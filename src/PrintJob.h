/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// needs EngineBase.h, SumatraDialogs.h and Print.h

/*
 * The custom print dialog's job: what goes on each sheet of paper, drawing a
 * sheet (on the printer or scaled down for the preview) and printing on a
 * background thread. Independent of the classic printing in Print.cpp.
 */

// Page Sizing & Handling
enum class PrintSizing {
    Size,
    Poster,
    Multiple,
    Booklet
};
enum class PrintSizeMode {
    Fit,
    Actual,
    Shrink,
    Custom
};
enum class PrintOrientation {
    Auto,
    Portrait,
    Landscape
};
enum class PrintDuplex {
    None,
    LongEdge,
    ShortEdge
};
enum class MultiPageOrder {
    Horizontal,
    HorizontalReversed,
    Vertical,
    VerticalReversed
};
enum class BookletSubset {
    BothSides,
    FrontOnly,
    BackOnly
};
enum class BookletBinding {
    Left,
    Right,
    LeftTall,
    RightTall
};
// Size: where a page sits inside the margins
enum class PrintPosition {
    Center,
    TopLeft,
    Top,
    TopRight,
    Left,
    Right,
    BottomLeft,
    Bottom,
    BottomRight
};
// Comments & Forms: what of a PDF page's annotations gets printed
enum class PrintAnnots {
    All,       // document and markups
    Document,  // page contents and form fields
    Stamps,    // page contents, form fields and stamps
    FormsOnly, // form fields only
};

struct PrintJobOptions {
    PrintSizing sizing = PrintSizing::Size;
    PrintSizeMode sizeMode = PrintSizeMode::Shrink;
    float customScale = 100; // percent
    PrintOrientation orientation = PrintOrientation::Auto;
    // Size: turns the page by this many degrees clockwise (0, 90, 180, 270)
    int rotation = 0;
    // Size: where the page sits inside the margins, then moved by the offset
    PrintPosition position = PrintPosition::Center;
    float offsetXMm = 0; // positive moves right
    float offsetYMm = 0; // positive moves down
    // margins from the paper's edges; the printer's own unprintable edges
    // apply too
    float marginLeftMm = 0;
    float marginTopMm = 0;
    float marginRightMm = 0;
    float marginBottomMm = 0;
    PrintDuplex duplex = PrintDuplex::None;
    bool grayscale = false;
    bool saveInk = false;
    bool reverse = false;
    int copies = 1;
    bool collate = true;
    // Current view: the visible part of one page, in page coordinates
    int viewPageNo = 0;
    RectF viewRect;

    float posterTileScale = 100; // percent
    float posterOverlapIn = 0;   // inches
    bool posterCutMarks = false;
    bool posterLabels = false;
    bool posterLargeOnly = false;

    int multiCols = 2;
    int multiRows = 1;
    MultiPageOrder multiOrder = MultiPageOrder::Horizontal;
    bool multiBorder = false;
    // Multiple and Booklet: turn pages to match their cell
    bool multiAutoRotate = false;

    BookletSubset bookletSubset = BookletSubset::BothSides;
    int bookletFirstSheet = 1;
    int bookletLastSheet = 0; // 0: the last one
    BookletBinding bookletBinding = BookletBinding::Left;

    PrintAnnots annots = PrintAnnots::All;
};

// the paper in device pixels; drawing coordinates start at printable's top-left
struct PrintGeometry {
    Size paper;
    Rect printable;
    float dpiX = 96;
    float dpiY = 96;
};

// a page, or part of it, placed on a sheet
struct PrintCell {
    int pageNo = 0;
    RectF pageRect; // page coordinates
    float zoom = 1;
    int rotation = 0;
    Point pos; // top-left of the rendered pageRect, drawing coordinates
    bool border = false;
};

// one side of a sheet of paper
struct PrintSheet {
    int firstCell = 0;
    int nCells = 0;
    // poster tile: area not shared with the previous tiles; label numbers are 1-based
    Rect cutRect;
    bool cutMarks = false;
    int labelCol = 0;
    int labelRow = 0;
    int labelPageNo = 0;
    // poster: the page this tile is cut from and where the tile sits on it
    int posterPageNo = 0;
    Point tileOrigin;
    Size tileSize;
};

struct PrintPlan {
    Vec<PrintCell> cells;
    Vec<PrintSheet> sheets;
};

// the pages to print, in order: ranges, odd / even, reversed
Vec<int> PrintPageList(const Vec<PRINTPAGERANGE>& ranges, int nPages, PrintRangeAdv range, bool reverse);
// paper orientation and duplex the job's layout needs, plus copies / color
void ApplyPrintJobToDevMode(DEVMODEW* dm, const PrintJobOptions& opts);
bool GetPrintGeometry(HDC hdc, PrintGeometry& geo);
// where pages go on the paper: the printable area inside the margins
Rect PrintContentArea(const PrintJobOptions& opts, const PrintGeometry& geo);
void BuildPrintPlan(EngineBase& engine, const Vec<int>& pages, const Print_Advanced_Data& adv,
                    const PrintJobOptions& opts, const PrintGeometry& geo, PrintPlan& plan);
// draws a sheet scaled by `scale` with drawing coordinates starting at `origin` (a preview)
void DrawPrintSheetPreview(EngineBase& engine, HDC hdc, const PrintPlan& plan, int sheetIdx,
                           const PrintJobOptions& opts, const PrintGeometry& geo, float scale, Point origin);

// prints on a background thread with a progress notification; outputFile is
// for Print to File
void PrintJobOnThread(MainWindow* win, EngineBase* engine, Str printerName, DEVMODEW* devMode,
                      const Vec<PRINTPAGERANGE>& ranges, const Print_Advanced_Data& adv, const PrintJobOptions& opts,
                      Str outputFile);
