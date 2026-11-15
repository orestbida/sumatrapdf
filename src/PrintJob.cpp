/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/Pixmap.h"
#include "base/ScopedWin.h"
#include "base/UITask.h"
#include "base/Win.h"

#include <mupdf/pdf.h>

#include "gui/UIModels.h"

#include "Settings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "base/GuessFileType.h"
#include "EngineAll.h"
#include "EngineMupdf.h"
#include "AppSettings.h"
#include "DisplayModel.h"
#include "ProgressUpdateUI.h"
#include "Notifications.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "SumatraDialogs.h"
#include "Translations.h"
#include "Print.h"
#include "PrintJob.h"

#include "SumatraLog.h"

// --- helpers ---

// virtual printers sometimes report zeros, giving a zoom of 0 or infinity
static bool IsValidPrintZoom(float zoom) {
    return zoom > 0 && isfinite(zoom);
}

// an image resolution override is for scans, image folders and comic books
static float PrintFileDPI(EngineBase& engine, const Print_Advanced_Data& adv) {
    float dpi = IsEngineImages(&engine) ? adv.dpiOverride : 0;
    if (!(dpi > 0) || !isfinite(dpi)) {
        dpi = engine.fileDPI;
    }
    if (!(dpi > 0) || !isfinite(dpi)) {
        dpi = 96.f;
    }
    return dpi;
}

// ranges can name pages past the end, e.g. after a reload with fewer pages
static Vec<PRINTPAGERANGE> ClampPageRanges(const Vec<PRINTPAGERANGE>& ranges, int nPages) {
    Vec<PRINTPAGERANGE> res;
    DWORD last = (DWORD)nPages;
    for (PRINTPAGERANGE r : ranges) {
        if (nPages < 1 || (r.nFromPage > last && r.nToPage > last)) {
            continue;
        }
        r.nFromPage = limitValue(r.nFromPage, (DWORD)1, last);
        r.nToPage = limitValue(r.nToPage, (DWORD)1, last);
        VecAppend(res, r);
    }
    return res;
}

// Print in grayscale and Save ink/toner (lighter colors).
// Returns the pixmap to use: a palette one is converted to BGRA first
static Pixmap* ApplyPrintTone(Pixmap* bmp, const PrintJobOptions& opts) {
    if (!opts.grayscale && !opts.saveInk) {
        return bmp;
    }
    if (bmp->format != PixmapFormat::BGR8 && bmp->format != PixmapFormat::BGRA8) {
        bmp = PixmapToBgra(bmp);
        if (!bmp || !bmp->data) {
            return bmp;
        }
    }
    int pixelSize = bmp->format == PixmapFormat::BGR8 ? 3 : 4;
    for (int y = 0; y < bmp->height; y++) {
        u8* px = bmp->data + (size_t)y * bmp->stride;
        for (int x = 0; x < bmp->width; x++, px += pixelSize) {
            if (opts.grayscale) {
                u8 gray = (u8)((px[2] * 77 + px[1] * 150 + px[0] * 29) >> 8);
                px[0] = px[1] = px[2] = gray;
            }
            if (opts.saveInk) {
                // move each channel a third of the way to white
                for (int i = 0; i < 3; i++) {
                    px[i] = (u8)(px[i] + (255 - px[i]) / 3);
                }
            }
        }
    }
    return bmp;
}

/*
 * A PDF page without some of its annotations (Comments & Forms other than
 * Document and Markups). The engine draws everything or nothing, so this
 * runs the page the way EngineMupdf::RenderPage does, under the same locks.
 */
static Pixmap* RenderPdfWithoutMarkups(EngineMupdf* e, int pageNo, float zoom, int rotation, RectF pageRect,
                                       PrintAnnots annots, RenderTarget target) {
    FzPageInfo* pi = e->GetFzPageInfo(pageNo, false);
    if (!pi || !pi->page || !e->pdfdoc) {
        return nullptr;
    }
    fz_context* ctx = e->Ctx();
    ScopedMutex renderScope(&e->renderLock);
    ScopedRecursiveMutex docScope(&e->docLock);
    fz_page* page = pi->page;
    fz_matrix ctm = e->viewctm(page, zoom, rotation);
    fz_irect bbox = fz_round_rect(fz_transform_rect(ToFzRect(pageRect), ctm));
    const char* usage = target == RenderTarget::Print ? "Print" : "View";

    fz_pixmap* pix = nullptr;
    fz_device* dev = nullptr;
    Pixmap* res = nullptr;
    fz_var(pix);
    fz_var(dev);
    fz_try(ctx) {
        pix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), bbox, nullptr, 0);
        fz_clear_pixmap_with_value(ctx, pix, 0xff);
        dev = fz_new_draw_device(ctx, ctm, pix);
        pdf_page* pdfPage = pdf_page_from_fz_page(ctx, page);
        if (annots != PrintAnnots::FormsOnly) {
            pdf_run_page_contents_with_usage(ctx, pdfPage, dev, fz_identity, usage, nullptr);
        }
        pdf_run_page_widgets_with_usage(ctx, pdfPage, dev, fz_identity, usage, nullptr);
        if (annots == PrintAnnots::Stamps) {
            for (pdf_annot* a = pdf_first_annot(ctx, pdfPage); a; a = pdf_next_annot(ctx, a)) {
                if (pdf_annot_type(ctx, a) == PDF_ANNOT_STAMP) {
                    pdf_run_annot(ctx, a, dev, fz_identity, nullptr);
                }
            }
        }
        fz_close_device(ctx, dev);

        // rgb -> a BGRA DIB
        res = AllocPixmapDIB(pix->w, pix->h);
        if (res && res->data) {
            for (int y = 0; y < pix->h; y++) {
                const u8* src = pix->samples + (size_t)y * pix->stride;
                u8* dst = res->data + (size_t)y * res->stride;
                for (int x = 0; x < pix->w; x++, src += pix->n, dst += 4) {
                    dst[0] = src[2];
                    dst[1] = src[1];
                    dst[2] = src[0];
                    dst[3] = 0xff;
                }
            }
        }
    }
    fz_always(ctx) {
        fz_drop_device(ctx, dev);
        fz_drop_pixmap(ctx, pix);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        FreePixmap(res);
        res = nullptr;
    }
    return res;
}

struct PrintJob;
static bool IsJobCanceled(PrintJob* job);

static Pixmap* RenderBand(EngineBase& engine, int pageNo, float zoom, int rotation, RectF* pageRect,
                          RenderTarget target, const PrintJobOptions& opts) {
    EngineMupdf* pdf = AsEngineMupdf(&engine);
    if (opts.annots != PrintAnnots::All && pdf && pdf->pdfdoc) {
        return RenderPdfWithoutMarkups(pdf, pageNo, zoom, rotation, *pageRect, opts.annots, target);
    }
    RenderPageArgs args(pageNo, zoom, rotation, pageRect, target);
    Pixmap* bmp = engine.RenderPage(args);
    // a page with few colors comes as a palette bitmap; some band widths of
    // those reach the printer blank (seen with Microsoft Print to PDF), so the
    // printer gets 24 bit ones
    if (target == RenderTarget::Print && bmp && bmp->format == PixmapFormat::Native) {
        bmp = PixmapToBgra(bmp);
    }
    // 24bpp: v4 (XPS) drivers render a 32bpp band as a slower RGBA PNG
    return PixmapToBgr(bmp);
}

/*
 * Renders pageRect of a page at `pos` on the DC in horizontal bands of the
 * output, which bounds the memory a page needs at printer resolution.
 */
static bool PrintPageInBands(EngineBase& engine, HDC hdc, int pageNo, float zoom, int rotation, const RectF& pageRect,
                             Point pos, RenderTarget target, const PrintJobOptions& opts, bool mirror, PrintJob* job) {
    if (!IsValidPrintZoom(zoom)) {
        return false;
    }
    RectF devFull = engine.Transform(pageRect, pageNo, zoom, rotation);
    int fullDx = (int)lroundf(devFull.dx);
    int fullDy = (int)lroundf(devFull.dy);
    if (fullDx <= 0 || fullDy <= 0) {
        return false;
    }
    const i64 kMaxBandBytes = 16LL * 1024 * 1024;
    int bandDy = (int)std::max((i64)1, kMaxBandBytes / ((i64)fullDx * 4));
    bandDy = std::min(bandDy, fullDy);

    bool anyOk = false;
    int y0 = 0;
    while (y0 < fullDy && !IsJobCanceled(job)) {
        int dy = std::min(bandDy, fullDy - y0);
        RectF devBand(devFull.x, devFull.y + (float)y0, devFull.dx, (float)dy);
        RectF pageBand = engine.Transform(devBand, pageNo, zoom, rotation, true);
        Pixmap* bmp = RenderBand(engine, pageNo, zoom, rotation, &pageBand, target, opts);
        if (bmp && bmp->data) {
            bmp = ApplyPrintTone(bmp, opts);
        }
        if (!bmp || !bmp->data) {
            FreePixmap(bmp);
            // out of memory: thinner bands before giving up
            if (bandDy > 1) {
                bandDy = std::max(1, bandDy / 2);
                continue;
            }
            break;
        }
        if (mirror) {
            FlipPixmapHorizontally(bmp);
        }
        // compute both edges the same way so adjacent bands meet with no seam
        Rect rc(pos.x, pos.y + y0, fullDx, dy);
        if (BlitPixmap(bmp, hdc, rc)) {
            anyOk = true;
        }
        FreePixmap(bmp);
        y0 += dy;
    }
    return anyOk;
}

// --- what goes on each sheet ---

Vec<int> PrintPageList(const Vec<PRINTPAGERANGE>& ranges, int nPages, PrintRangeAdv range, bool reverse) {
    Vec<int> pages;
    for (PRINTPAGERANGE r : ClampPageRanges(ranges, nPages)) {
        int dir = r.nFromPage > r.nToPage ? -1 : 1;
        for (int pageNo = (int)r.nFromPage; pageNo != (int)r.nToPage + dir; pageNo += dir) {
            bool isEven = pageNo % 2 == 0;
            if ((range == PrintRangeAdv::Even && !isEven) || (range == PrintRangeAdv::Odd && isEven)) {
                continue;
            }
            VecAppend(pages, pageNo);
        }
    }
    if (reverse) {
        VecReverse(pages);
    }
    return pages;
}

void ApplyPrintJobToDevMode(DEVMODEW* dm, const PrintJobOptions& opts) {
    dm->dmCopies = (short)std::max(1, opts.copies);
    dm->dmCollate = opts.collate ? DMCOLLATE_TRUE : DMCOLLATE_FALSE;
    dm->dmFields |= DM_COPIES | DM_COLLATE;
    if (opts.grayscale) {
        dm->dmColor = DMCOLOR_MONOCHROME;
        dm->dmFields |= DM_COLOR;
    }

    // the paper orientation; Auto in Size / Poster keeps the printer's and
    // turns the pages instead
    PrintOrientation orient = opts.orientation;
    bool tallBooklet =
        opts.bookletBinding == BookletBinding::LeftTall || opts.bookletBinding == BookletBinding::RightTall;
    if (opts.sizing == PrintSizing::Booklet && orient == PrintOrientation::Auto) {
        orient = tallBooklet ? PrintOrientation::Portrait : PrintOrientation::Landscape;
    } else if (opts.sizing == PrintSizing::Multiple && orient == PrintOrientation::Auto) {
        orient = opts.multiCols > opts.multiRows ? PrintOrientation::Landscape : PrintOrientation::Portrait;
    }
    if (orient != PrintOrientation::Auto) {
        dm->dmOrientation = orient == PrintOrientation::Portrait ? DMORIENT_PORTRAIT : DMORIENT_LANDSCAPE;
        dm->dmFields |= DM_ORIENTATION;
    }

    // a two-sided booklet is folded across its binding edge
    PrintDuplex duplex = opts.duplex;
    if (opts.sizing == PrintSizing::Booklet && opts.bookletSubset == BookletSubset::BothSides) {
        duplex = orient == PrintOrientation::Portrait ? PrintDuplex::LongEdge : PrintDuplex::ShortEdge;
    }
    dm->dmDuplex = DMDUP_SIMPLEX;
    if (duplex == PrintDuplex::LongEdge) {
        dm->dmDuplex = DMDUP_VERTICAL;
    } else if (duplex == PrintDuplex::ShortEdge) {
        dm->dmDuplex = DMDUP_HORIZONTAL;
    }
    dm->dmFields |= DM_DUPLEX;
}

bool GetPrintGeometry(HDC hdc, PrintGeometry& geo) {
    geo.paper = Size(GetDeviceCaps(hdc, PHYSICALWIDTH), GetDeviceCaps(hdc, PHYSICALHEIGHT));
    geo.printable = Rect(GetDeviceCaps(hdc, PHYSICALOFFSETX), GetDeviceCaps(hdc, PHYSICALOFFSETY),
                         GetDeviceCaps(hdc, HORZRES), GetDeviceCaps(hdc, VERTRES));
    geo.dpiX = (float)GetDeviceCaps(hdc, LOGPIXELSX);
    geo.dpiY = (float)GetDeviceCaps(hdc, LOGPIXELSY);
    // virtual printers sometimes report zeros (see PrintToDevice)
    if (!(geo.dpiX > 0) || !(geo.dpiY > 0)) {
        geo.dpiX = geo.dpiY = 96.f;
    }
    if (geo.paper.dx <= 0 || geo.paper.dy <= 0) {
        geo.paper = Size((int)(8.5f * geo.dpiX), (int)(11.f * geo.dpiY));
    }
    if (geo.printable.dx <= 0 || geo.printable.dy <= 0) {
        geo.printable = Rect(0, 0, geo.paper.dx, geo.paper.dy);
    }
    return true;
}

static bool IsWide(float dx, float dy) {
    return dx > dy;
}

Rect PrintContentArea(const PrintJobOptions& opts, const PrintGeometry& geo) {
    float pxPerMmX = geo.dpiX / 25.4f;
    float pxPerMmY = geo.dpiY / 25.4f;
    Rect pr = geo.printable;
    int left = std::max(pr.x, (int)(opts.marginLeftMm * pxPerMmX));
    int top = std::max(pr.y, (int)(opts.marginTopMm * pxPerMmY));
    int right = std::min(pr.x + pr.dx, geo.paper.dx - (int)(opts.marginRightMm * pxPerMmX));
    int bottom = std::min(pr.y + pr.dy, geo.paper.dy - (int)(opts.marginBottomMm * pxPerMmY));
    // margins that leave no room are ignored
    if (right - left < 1 || bottom - top < 1) {
        return pr;
    }
    return Rect(left, top, right - left, bottom - top);
}

// -1, 0, 1: left / center / right and top / middle / bottom
static void PositionAlign(PrintPosition pos, int& alignX, int& alignY) {
    static const int kAlign[][2] = {{0, 0}, {-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
    alignX = kAlign[(int)pos][0];
    alignY = kAlign[(int)pos][1];
}

/*
 * Places src (page coordinates) in area (paper coordinates) at `position`,
 * then moved by `nudge` device pixels. Fit and Shrink scale it to the area,
 * Actual and Custom use actualZoom. autoRotate turns it 90 degrees when that
 * matches the area's orientation.
 */
static PrintCell PlacePage(EngineBase& engine, int pageNo, RectF src, Rect area, PrintSizeMode mode, float actualZoom,
                           bool autoRotate, int extraRotation, const PrintGeometry& geo,
                           PrintPosition position = PrintPosition::Center, Point nudge = {}) {
    SizeF size = src.Size();
    size.dx = std::max(size.dx, 1.f);
    size.dy = std::max(size.dy, 1.f);
    int rotation = extraRotation % 360;
    if (rotation % 180 != 0) {
        std::swap(size.dx, size.dy);
    }
    if (autoRotate && size.dx != size.dy && IsWide(size.dx, size.dy) != IsWide((float)area.dx, (float)area.dy)) {
        rotation = (rotation + 90) % 360;
        std::swap(size.dx, size.dy);
    }

    float fitZoom = std::min((float)area.dx / size.dx, (float)area.dy / size.dy);
    float zoom = actualZoom;
    if (mode == PrintSizeMode::Fit) {
        zoom = fitZoom;
    } else if (mode == PrintSizeMode::Shrink) {
        zoom = std::min(fitZoom, actualZoom);
    }
    if (!IsValidPrintZoom(zoom)) {
        zoom = 1.f;
    }

    PrintCell cell;
    cell.pageNo = pageNo;
    cell.pageRect = src;
    cell.zoom = zoom;
    cell.rotation = rotation;
    int alignX, alignY;
    PositionAlign(position, alignX, alignY);
    float spareDx = (float)area.dx - size.dx * zoom;
    float spareDy = (float)area.dy - size.dy * zoom;
    cell.pos.x = area.x + (int)(spareDx * (alignX + 1) / 2) + nudge.x - geo.printable.x;
    cell.pos.y = area.y + (int)(spareDy * (alignY + 1) / 2) + nudge.y - geo.printable.y;
    return cell;
}

static PrintSheet* AddPrintSheet(PrintPlan& plan) {
    PrintSheet* sheet = VecAppendBlanks(plan.sheets, 1);
    *sheet = PrintSheet{};
    sheet->firstCell = len(plan.cells);
    return sheet;
}

static void AddPrintCell(PrintPlan& plan, const PrintCell& cell) {
    VecAppend(plan.cells, cell);
    plan.sheets[len(plan.sheets) - 1].nCells++;
}

static void PlanSizeSheets(EngineBase& engine, const Vec<int>& pages, const Print_Advanced_Data& adv,
                           const PrintJobOptions& opts, const PrintGeometry& geo, float dpiFactor, PrintPlan& plan) {
    float actualZoom = dpiFactor;
    if (opts.sizeMode == PrintSizeMode::Custom) {
        actualZoom = dpiFactor * opts.customScale / 100.f;
    }
    // a rotation chosen by the user replaces Auto's turning to fit
    bool autoRotate = opts.orientation == PrintOrientation::Auto && adv.autoRotate && opts.rotation == 0;
    int rotation = (adv.extraRotation + opts.rotation) % 360;
    Rect area = PrintContentArea(opts, geo);
    Point nudge((int)(opts.offsetXMm * geo.dpiX / 25.4f), (int)(opts.offsetYMm * geo.dpiY / 25.4f));
    if (opts.viewPageNo > 0) {
        AddPrintSheet(plan);
        AddPrintCell(plan, PlacePage(engine, opts.viewPageNo, opts.viewRect, area, opts.sizeMode, actualZoom,
                                     autoRotate, rotation, geo, opts.position, nudge));
        return;
    }
    for (int pageNo : pages) {
        AddPrintSheet(plan);
        RectF src = engine.PageMediabox(pageNo);
        AddPrintCell(plan, PlacePage(engine, pageNo, src, area, opts.sizeMode, actualZoom, autoRotate, rotation, geo,
                                     opts.position, nudge));
    }
}

static void PlanMultipleSheets(EngineBase& engine, const Vec<int>& pages, const Print_Advanced_Data& adv,
                               const PrintJobOptions& opts, const PrintGeometry& geo, PrintPlan& plan) {
    int cols = std::max(1, opts.multiCols);
    int rows = std::max(1, opts.multiRows);
    int perSheet = cols * rows;
    Rect area = PrintContentArea(opts, geo);
    int cellDx = area.dx / cols;
    int cellDy = area.dy / rows;
    int gap = std::min(cellDx, cellDy) / 30;

    for (int i = 0; i < len(pages); i++) {
        int idx = i % perSheet;
        if (idx == 0) {
            AddPrintSheet(plan);
        }
        int col = idx % cols;
        int row = idx / cols;
        if (opts.multiOrder == MultiPageOrder::Vertical || opts.multiOrder == MultiPageOrder::VerticalReversed) {
            col = idx / rows;
            row = idx % rows;
        }
        if (opts.multiOrder == MultiPageOrder::HorizontalReversed ||
            opts.multiOrder == MultiPageOrder::VerticalReversed) {
            col = cols - 1 - col;
        }
        Rect cellArea(area.x + col * cellDx + gap, area.y + row * cellDy + gap, cellDx - 2 * gap, cellDy - 2 * gap);
        int pageNo = pages[i];
        PrintCell cell = PlacePage(engine, pageNo, engine.PageMediabox(pageNo), cellArea, PrintSizeMode::Fit, 1.f,
                                   opts.multiAutoRotate, adv.extraRotation, geo);
        cell.border = opts.multiBorder;
        AddPrintCell(plan, cell);
    }
}

/*
 * Each page enlarged by the tile scale and cut into tiles the size of the
 * area inside the margins, overlapping by posterOverlapMm, left to right, top
 * to bottom.
 */
static void PlanPosterSheets(EngineBase& engine, const Vec<int>& pages, const Print_Advanced_Data& adv,
                             const PrintJobOptions& opts, const PrintGeometry& geo, float dpiFactor, PrintPlan& plan) {
    float zoom = dpiFactor * std::max(opts.posterTileScale, 1.f) / 100.f;
    int rotation = adv.extraRotation % 360;
    Rect area = PrintContentArea(opts, geo);
    // where a tile goes, in drawing coordinates
    Point tilePos(area.x - geo.printable.x, area.y - geo.printable.y);
    int tileDx = area.dx;
    int tileDy = area.dy;
    int overlapX = std::min((int)(opts.posterOverlapMm * geo.dpiX / 25.4f), tileDx / 2);
    int overlapY = std::min((int)(opts.posterOverlapMm * geo.dpiY / 25.4f), tileDy / 2);

    for (int pageNo : pages) {
        RectF mediabox = engine.PageMediabox(pageNo);
        RectF devFull = engine.Transform(mediabox, pageNo, zoom, rotation);
        int fullDx = (int)lroundf(devFull.dx);
        int fullDy = (int)lroundf(devFull.dy);
        if (opts.posterLargeOnly && fullDx <= tileDx && fullDy <= tileDy) {
            PrintSheet* sheet = AddPrintSheet(plan);
            sheet->posterPageNo = pageNo;
            sheet->tileSize = Size(tileDx, tileDy);
            AddPrintCell(plan,
                         PlacePage(engine, pageNo, mediabox, area, PrintSizeMode::Actual, zoom, false, rotation, geo));
            continue;
        }
        int nCols = fullDx <= tileDx ? 1 : (fullDx - overlapX + tileDx - overlapX - 1) / (tileDx - overlapX);
        int nRows = fullDy <= tileDy ? 1 : (fullDy - overlapY + tileDy - overlapY - 1) / (tileDy - overlapY);
        for (int row = 0; row < nRows; row++) {
            for (int col = 0; col < nCols; col++) {
                int x = col * (tileDx - overlapX);
                int y = row * (tileDy - overlapY);
                int dx = std::min(tileDx, fullDx - x);
                int dy = std::min(tileDy, fullDy - y);
                if (dx <= 0 || dy <= 0) {
                    continue;
                }
                PrintSheet* sheet = AddPrintSheet(plan);
                int cutX = col > 0 ? overlapX : 0;
                int cutY = row > 0 ? overlapY : 0;
                sheet->cutRect = Rect(tilePos.x + cutX, tilePos.y + cutY, dx - cutX, dy - cutY);
                sheet->posterPageNo = pageNo;
                sheet->tileOrigin = Point(x, y);
                sheet->tileSize = Size(dx, dy);
                sheet->cutMarks = opts.posterCutMarks;
                if (opts.posterLabels) {
                    sheet->labelCol = col + 1;
                    sheet->labelRow = row + 1;
                    sheet->labelPageNo = pageNo;
                }
                RectF devTile(devFull.x + (float)x, devFull.y + (float)y, (float)dx, (float)dy);
                PrintCell cell;
                cell.pageNo = pageNo;
                cell.pageRect = engine.Transform(devTile, pageNo, zoom, rotation, true);
                cell.zoom = zoom;
                cell.rotation = rotation;
                cell.pos = tilePos;
                AddPrintCell(plan, cell);
            }
        }
    }
}

/*
 * Booklet: n pages padded to a multiple of 4, two pages on each side of a sheet
 * that is folded in half. For 8 pages (left | right):
 *   sheet 1 front: 8 | 1   back: 2 | 7
 *   sheet 2 front: 6 | 3   back: 4 | 5
 * Right binding swaps left and right; on portrait paper (tall bindings) the
 * two pages go top and bottom.
 */
static void PlanBookletSheets(EngineBase& engine, const Vec<int>& pages, const Print_Advanced_Data& adv,
                              const PrintJobOptions& opts, const PrintGeometry& geo, PrintPlan& plan) {
    int n = len(pages);
    if (n == 0) {
        return;
    }
    int n4 = (n + 3) / 4 * 4;
    int nSheets = n4 / 4;
    int first = limitValue(opts.bookletFirstSheet, 1, nSheets);
    int last = opts.bookletLastSheet <= 0 ? nSheets : limitValue(opts.bookletLastSheet, first, nSheets);
    // wide paper is folded left | right, tall paper top / bottom
    Rect pr = PrintContentArea(opts, geo);
    bool tall = pr.dy > pr.dx;
    bool rightBinding =
        opts.bookletBinding == BookletBinding::Right || opts.bookletBinding == BookletBinding::RightTall;

    Rect halves[2] = {Rect(pr.x, pr.y, pr.dx / 2, pr.dy), Rect(pr.x + pr.dx / 2, pr.y, pr.dx / 2, pr.dy)};
    if (tall) {
        halves[0] = Rect(pr.x, pr.y, pr.dx, pr.dy / 2);
        halves[1] = Rect(pr.x, pr.y + pr.dy / 2, pr.dx, pr.dy / 2);
    }

    auto addSide = [&](int leftIdx, int rightIdx) {
        AddPrintSheet(plan);
        int idxs[2] = {leftIdx, rightIdx};
        if (rightBinding) {
            std::swap(idxs[0], idxs[1]);
        }
        for (int i = 0; i < 2; i++) {
            if (idxs[i] >= n) {
                continue; // a blank page
            }
            int pageNo = pages[idxs[i]];
            AddPrintCell(plan, PlacePage(engine, pageNo, engine.PageMediabox(pageNo), halves[i], PrintSizeMode::Fit,
                                         1.f, opts.multiAutoRotate, adv.extraRotation, geo));
        }
    };
    for (int s = first - 1; s < last; s++) {
        if (opts.bookletSubset != BookletSubset::BackOnly) {
            addSide(n4 - 1 - 2 * s, 2 * s);
        }
        if (opts.bookletSubset != BookletSubset::FrontOnly) {
            addSide(2 * s + 1, n4 - 2 - 2 * s);
        }
    }
}

void BuildPrintPlan(EngineBase& engine, const Vec<int>& pages, const Print_Advanced_Data& adv,
                    const PrintJobOptions& opts, const PrintGeometry& geo, PrintPlan& plan) {
    VecReset(plan.cells);
    VecReset(plan.sheets);

    float fileDPI = PrintFileDPI(engine, adv);
    float dpiFactor = std::min(geo.dpiX, geo.dpiY) / fileDPI;
    if (!IsValidPrintZoom(dpiFactor)) {
        dpiFactor = 1.f;
    }
    switch (opts.sizing) {
        case PrintSizing::Size:
            PlanSizeSheets(engine, pages, adv, opts, geo, dpiFactor, plan);
            break;
        case PrintSizing::Poster:
            PlanPosterSheets(engine, pages, adv, opts, geo, dpiFactor, plan);
            break;
        case PrintSizing::Multiple:
            PlanMultipleSheets(engine, pages, adv, opts, geo, plan);
            break;
        case PrintSizing::Booklet:
            PlanBookletSheets(engine, pages, adv, opts, geo, plan);
            break;
    }
}

static HFONT CreatePrintFont(float dy) {
    return CreateFontW(-std::max(1, (int)dy), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

static void DrawSheetLine(HDC hdc, Point from, Point to, int penDx, int penStyle) {
    HPEN pen = CreatePen(penStyle, penDx, RGB(0, 0, 0));
    HGDIOBJ prev = SelectObject(hdc, pen);
    MoveToEx(hdc, from.x, from.y, nullptr);
    LineTo(hdc, to.x, to.y);
    SelectObject(hdc, prev);
    DeleteObject(pen);
}

// black text on white, for poster labels
static void DrawSheetText(HDC hdc, Str text, Rect rc, float fontDy) {
    HFONT font = CreatePrintFont(fontDy);
    HGDIOBJ prev = SelectObject(hdc, font);
    SetTextColor(hdc, RGB(0, 0, 0));
    SetBkColor(hdc, RGB(255, 255, 255));
    SetBkMode(hdc, OPAQUE);
    RECT r = ToRECT(rc);
    WCHAR* ws = CWStrTemp(text);
    DrawTextW(hdc, ws, -1, &r, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | DT_LEFT | DT_TOP);
    SelectObject(hdc, prev);
    DeleteObject(font);
}

/*
 * Draws one sheet. Drawing coordinates are scaled by `scale` and start at
 * `origin`: (1, printable top-left) on the printer, smaller for the preview.
 */
static void DrawPrintSheet(EngineBase& engine, HDC hdc, const PrintPlan& plan, int sheetIdx,
                           const PrintJobOptions& opts, const PrintGeometry& geo, float scale, Point origin,
                           RenderTarget target, bool mirror, PrintJob* job) {
    const PrintSheet& sheet = plan.sheets[sheetIdx];
    auto toDraw = [&](int x, int y) { return Point(origin.x + (int)(x * scale), origin.y + (int)(y * scale)); };
    int penDx = std::max(1, (int)(geo.dpiX / 150.f * scale));

    for (int i = sheet.firstCell; i < sheet.firstCell + sheet.nCells; i++) {
        const PrintCell& cell = plan.cells[i];
        float zoom = cell.zoom * scale;
        Point pos = toDraw(cell.pos.x, cell.pos.y);
        PrintPageInBands(engine, hdc, cell.pageNo, zoom, cell.rotation, cell.pageRect, pos, target, opts, mirror, job);
        if (!cell.border) {
            continue;
        }
        RectF onSheet = engine.Transform(cell.pageRect, cell.pageNo, zoom, cell.rotation);
        Rect rc(pos.x, pos.y, (int)lroundf(onSheet.dx), (int)lroundf(onSheet.dy));
        HPEN pen = CreatePen(PS_SOLID, penDx, RGB(0, 0, 0));
        HGDIOBJ prevPen = SelectObject(hdc, pen);
        HGDIOBJ prevBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, rc.x, rc.y, rc.x + rc.dx, rc.y + rc.dy);
        SelectObject(hdc, prevBrush);
        SelectObject(hdc, prevPen);
        DeleteObject(pen);
    }

    // poster: dashed lines where this tile overlaps the previous ones
    Rect cut = sheet.cutRect;
    if (sheet.cutMarks && cut.x > 0) {
        DrawSheetLine(hdc, toDraw(cut.x, cut.y), toDraw(cut.x, cut.y + cut.dy), penDx, PS_DOT);
    }
    if (sheet.cutMarks && cut.y > 0) {
        DrawSheetLine(hdc, toDraw(cut.x, cut.y), toDraw(cut.x + cut.dx, cut.y), penDx, PS_DOT);
    }
    if (sheet.labelPageNo > 0) {
        TempStr label = fmt("(%d,%d) %s %s %d", sheet.labelCol, sheet.labelRow,
                            path::GetBaseNameTemp(engine.FilePath()), Tr("page"), sheet.labelPageNo);
        float fontDy = geo.dpiY * 8.f / 72.f * scale;
        Point tl = toDraw(cut.x, cut.y + cut.dy);
        tl.y -= (int)(fontDy * 1.5f);
        Rect rc(tl.x, tl.y, (int)(cut.dx * scale), (int)(fontDy * 1.5f));
        DrawSheetText(hdc, label, rc, fontDy);
    }
}

void DrawPrintSheetPreview(EngineBase& engine, HDC hdc, const PrintPlan& plan, int sheetIdx,
                           const PrintJobOptions& opts, const PrintGeometry& geo, float scale, Point origin) {
    if (sheetIdx < 0 || sheetIdx >= len(plan.sheets)) {
        return;
    }
    DrawPrintSheet(engine, hdc, plan, sheetIdx, opts, geo, scale, origin, RenderTarget::View, false, nullptr);
}

// --- printing on a background thread ---

struct PrintJob {
    MainWindow* win = nullptr;
    EngineBase* engine = nullptr; // our own copy, read only by the print thread
    Str printerName;
    Str outputFile;
    DEVMODEW* devMode = nullptr;
    Vec<PRINTPAGERANGE> ranges;
    Print_Advanced_Data adv;
    PrintJobOptions opts;
    bool mirror = false;

    NotificationWnd* notif = nullptr;
    AtomicBool isCanceled = 0;
    ThreadHandle thread = nullptr;

    ~PrintJob() {
        SafeEngineRelease(&engine);
        str::Free(printerName);
        str::Free(outputFile);
        free(devMode);
        SafeCloseThreadHandle(&thread);
    }
};

// closing the progress notification, closing the window or AbortPrinting()
static bool IsJobCanceled(PrintJob* job) {
    if (!job) {
        return false;
    }
    return AtomicBoolGet(&job->isCanceled) || !IsMainWindowValidAndNotClosing(job->win) ||
           AtomicBoolGet(&job->win->printCanceled);
}

struct JobProgress {
    PrintJob* job;
    int current;
    int total;
};

static void UpdateJobProgress(JobProgress* p) {
    if (p->job->notif && IsMainWindowValid(p->job->win)) {
        TempStr msg = fmt(Tr("Printing page %d of %d...").s, p->current, p->total);
        UpdateNotificationProgress(p->job->notif, msg, CalcPerc(p->current, p->total));
    }
    delete p;
}

static void OnJobNotifClosed(PrintJob* job, NotificationClosedEvent*) {
    AtomicBoolSet(&job->isCanceled, true);
    job->notif = nullptr;
}

static bool PrintPlanToDevice(PrintJob* job, HDC hdc, const PrintPlan& plan, const PrintGeometry& geo) {
    int total = len(plan.sheets);
    for (int i = 0; i < total; i++) {
        if (IsJobCanceled(job)) {
            return false;
        }
        uitask::Post(MkFunc0(UpdateJobProgress, new JobProgress{job, i + 1, total}), nullptr);
        if (StartPage(hdc) <= 0) {
            logf("PrintJob: StartPage() failed\n");
            return false;
        }
        DrawPrintSheet(*job->engine, hdc, plan, i, job->opts, geo, 1.f, Point(0, 0), RenderTarget::Print, job->mirror,
                       job);
        if (EndPage(hdc) <= 0 || IsJobCanceled(job)) {
            logf("PrintJob: EndPage() failed or canceled\n");
            return false;
        }
    }
    return true;
}

static bool PrintJobToDevice(PrintJob* job) {
    EngineBase& engine = *job->engine;
    EnsureFullLayout(job->engine);
    logf("PrintJob: printer: '%s', file: '%s'\n", job->printerName, engine.FilePath());

    // let the driver merge the public fields (orientation, duplex, copies) into its private data
    WCHAR* printerName = CWStrTemp(job->printerName);
    HANDLE hPrinter = nullptr;
    if (OpenPrinterW(printerName, &hPrinter, nullptr) && hPrinter) {
        DocumentPropertiesW(nullptr, hPrinter, printerName, job->devMode, job->devMode, DM_IN_BUFFER | DM_OUT_BUFFER);
        ClosePrinter(hPrinter);
    }
    AutoDeleteDC hdc{CreateDCW(nullptr, printerName, nullptr, job->devMode)};
    if (!hdc) {
        logf("PrintJob: CreateDCW('%s') failed\n", job->printerName);
        return false;
    }

    DOCINFOW di{};
    di.cbSize = sizeof(di);
    // just the file name: some spoolers choke on long or non-ASCII paths (issue #2166)
    di.lpszDocName = CWStrTemp(path::GetBaseNameTemp(engine.FilePath()));
    if (len(job->outputFile) > 0) {
        di.lpszOutput = CWStrTemp(job->outputFile);
    }
    if (StartDoc(hdc, &di) <= 0) {
        logf("PrintJob: StartDoc() failed\n");
        return false;
    }
    SetMapMode(hdc, MM_TEXT);

    PrintGeometry geo;
    GetPrintGeometry(hdc, geo);
    logf("PrintJob: %g x %g dpi, paper %d x %d\n", geo.dpiX, geo.dpiY, geo.paper.dx, geo.paper.dy);
    Vec<int> pages = PrintPageList(job->ranges, engine.PageCount(), job->adv.range, job->opts.reverse);
    PrintPlan plan;
    BuildPrintPlan(engine, pages, job->adv, job->opts, geo, plan);
    if (!PrintPlanToDevice(job, hdc, plan, geo)) {
        AbortDoc(hdc);
        return false;
    }
    if (EndDoc(hdc) <= 0) {
        logf("PrintJob: EndDoc() failed\n");
        return false;
    }
    logf("PrintJob: finished ok\n");
    return true;
}

static void PrintJobFinished(PrintJob* job) {
    MainWindow* win = job->win;
    if (IsMainWindowValid(win)) {
        if (win->printThread == job->thread) {
            win->printThread = nullptr;
        }
        if (job->notif) {
            RemoveNotification(job->notif);
        }
    }
    delete job;
}

static void PrintJobThread(PrintJob* job) {
    PrintJobToDevice(job);
    uitask::Post(MkFunc0(PrintJobFinished, job), "PrintJobFinished");
    DestroyTempArena();
}

void PrintJobOnThread(MainWindow* win, EngineBase* engine, Str printerName, DEVMODEW* devMode,
                      const Vec<PRINTPAGERANGE>& ranges, const Print_Advanced_Data& adv, const PrintJobOptions& opts,
                      Str outputFile) {
    AbortPrinting(win);

    auto* job = new PrintJob();
    job->win = win;
    // the print thread gets its own engine: Clone() re-reads the file. A file
    // that is gone since opening prints from the open one (issue #5790)
    job->engine = engine->Clone();
    if (!job->engine) {
        job->engine = engine;
        job->engine->AddRef();
    }
    job->printerName = str::Dup(printerName);
    job->outputFile = str::Dup(outputFile);
    job->devMode = (DEVMODEW*)MemDup(nullptr, devMode, (size_t)devMode->dmSize + devMode->dmDriverExtra);
    job->ranges = ranges;
    job->adv = adv;
    job->opts = opts;
    if (DisplayModel* dm = win->AsFixed()) {
        job->mirror = dm->GetMirror();
    }

    NotificationCreateArgs args;
    SetNotifWindow(args, win);
    args.timeoutMs = 0;
    args.groupId = nullptr;
    args.onClosed = MkFunc1<PrintJob, NotificationClosedEvent*>(OnJobNotifClosed, job);
    job->notif = ShowNotification(args);

    job->thread = StartThread(MkFunc0(PrintJobThread, job), StrL("PrintJobThread"));
    if (!job->thread) {
        logf("PrintJobOnThread: couldn't start the print thread\n");
        PrintJobFinished(job);
        return;
    }
    win->printThread = job->thread;
}
