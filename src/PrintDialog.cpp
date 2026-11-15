/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/Pixmap.h"
#include "base/Win.h"

#include <commdlg.h>

#include "gui/UIModels.h"
#include "gui/Dpi.h"

#include "Settings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "base/GuessFileType.h"
#include "EngineAll.h"
#include "AppSettings.h"
#include "DisplayModel.h"
#include "Annotation.h"
#include "TextSelection.h"
#include "Notifications.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "AIChatTypes.h"
#include "WindowTab.h"
#include "SumatraDialogs.h"
#include "Flags.h"
#include "Theme.h"
#include "Translations.h"
#include "DarkMode.h"
#include "SumatraPDFCommon.h"
#include "Print.h"
#include "PrintJob.h"
#include "PrintDialog.h"

#include "SumatraLog.h"

/*
 * The print dialog:
 *
 *   Printer: [.............v] [Properties]                       Help (?)
 *   Copies:  [1 ^v] [x] Collate [ ] Print to file  [ ] Grayscale [ ] Save ink
 *   +- Pages to Print ------------------+  +- preview ---------------+
 *   | (o) All ( ) Current ( ) Pages [ ] |  | Scale: 94%              |
 *   | > More Options                    |  |   210 x 297 mm          |
 *   +- Page Sizing & Handling ----------+  |  +-------+              |
 *   | [Size][Poster][Multiple][Booklet] |  |  | sheet |              |
 *   | options of the selected one       |  |  +-------+              |
 *   +-----------------------------------+  | [<] ======o====== [>]   |
 *   | [ ] Print on both sides of paper  |  |      Page 1 of 2        |
 *   | Orientation: (o) Auto ( ) Portr.. |  +- Paper -----------------+
 *   +- Comments & Forms ----------------+  | Size, Source, Quality,  |
 *                                          | Margins                 |
 *                                                    [Print] [Cancel]
 *
 * More Options, the Size extras, the two-sided rows and the auto-rotate row
 * come and go with the choices; the rows below them move up and the dialog
 * shrinks.
 */

enum class PagesToPrint {
    All,
    Current,
    Pages,
    CurrentView
};

// control ids of the print dialog
enum {
    IDC_PR_FIRST = 1000,
    IDC_PR_PRINTER_LABEL,
    IDC_PR_PRINTER,
    IDC_PR_PROPERTIES,
    IDC_PR_HELP,
    IDC_PR_HELP_ICON,
    IDC_PR_COPIES_LABEL,
    IDC_PR_COPIES,
    IDC_PR_COPIES_SPIN,
    IDC_PR_COLLATE,
    IDC_PR_TO_FILE,
    IDC_PR_GRAYSCALE,
    IDC_PR_SAVE_INK,
    IDC_PR_PAGES_TITLE,
    IDC_PR_ALL,
    IDC_PR_CURRENT,
    IDC_PR_PAGES,
    IDC_PR_CURRENT_VIEW,
    IDC_PR_PAGES_EDIT,
    IDC_PR_MORE_ARROW,
    IDC_PR_MORE,
    IDC_PR_REVERSE,
    IDC_PR_ODD_EVEN_LABEL,
    IDC_PR_ODD_EVEN,
    IDC_PR_SIZING_TITLE,
    IDC_PR_SIZE,
    IDC_PR_POSTER,
    IDC_PR_MULTIPLE,
    IDC_PR_BOOKLET,
    IDC_PR_FIT,
    IDC_PR_ACTUAL,
    IDC_PR_SHRINK,
    IDC_PR_CUSTOM,
    IDC_PR_CUSTOM_EDIT,
    IDC_PR_CUSTOM_PERCENT,
    IDC_PR_ROTATE_LABEL,
    IDC_PR_ROTATE,
    IDC_PR_POSITION_LABEL,
    IDC_PR_POSITION,
    IDC_PR_OFFSET_LABEL,
    IDC_PR_OFFSET_X,
    IDC_PR_OFFSET_Y_LABEL,
    IDC_PR_OFFSET_Y,
    IDC_PR_OFFSET_X_UNIT,
    IDC_PR_OFFSET_UNIT,
    IDC_PR_TILE_LABEL,
    IDC_PR_TILE_EDIT,
    IDC_PR_TILE_PERCENT,
    IDC_PR_OVERLAP_LABEL,
    IDC_PR_OVERLAP_EDIT,
    IDC_PR_OVERLAP_UNIT,
    IDC_PR_CUT_MARKS,
    IDC_PR_LABELS,
    IDC_PR_LARGE_ONLY,
    IDC_PR_PER_SHEET_LABEL,
    IDC_PR_PER_SHEET,
    IDC_PR_COLS_EDIT,
    IDC_PR_BY_LABEL,
    IDC_PR_ROWS_EDIT,
    IDC_PR_ORDER_LABEL,
    IDC_PR_ORDER,
    IDC_PR_BORDER,
    IDC_PR_SUBSET_LABEL,
    IDC_PR_SUBSET,
    IDC_PR_SHEETS_LABEL,
    IDC_PR_SHEETS_FROM,
    IDC_PR_SHEETS_TO_LABEL,
    IDC_PR_SHEETS_TO,
    IDC_PR_BINDING_LABEL,
    IDC_PR_BINDING,
    IDC_PR_DUPLEX,
    IDC_PR_FLIP_LONG,
    IDC_PR_FLIP_SHORT,
    IDC_PR_ORIENT_LABEL,
    IDC_PR_ORIENT_AUTO,
    IDC_PR_ORIENT_PORTRAIT,
    IDC_PR_ORIENT_LANDSCAPE,
    IDC_PR_AUTO_ROTATE,
    IDC_PR_COMMENTS_TITLE,
    IDC_PR_COMMENTS,
    IDC_PR_SCALE_LABEL,
    IDC_PR_PREVIEW,
    IDC_PR_PREV,
    IDC_PR_SLIDER,
    IDC_PR_NEXT,
    IDC_PR_SHEET_LABEL,
    IDC_PR_GROUP_PAPER,
    IDC_PR_PAPER_TITLE,
    IDC_PR_PAPER_SIZE_LABEL,
    IDC_PR_PAPER_SIZE,
    IDC_PR_SOURCE_LABEL,
    IDC_PR_SOURCE,
    IDC_PR_QUALITY_LABEL,
    IDC_PR_QUALITY,
    IDC_PR_MARGINS_LABEL,
    IDC_PR_MARGIN_LEFT_LABEL,
    IDC_PR_MARGIN_LEFT,
    IDC_PR_MARGIN_RIGHT_LABEL,
    IDC_PR_MARGIN_RIGHT,
    IDC_PR_MARGIN_TOP_LABEL,
    IDC_PR_MARGIN_TOP,
    IDC_PR_MARGIN_BOTTOM_LABEL,
    IDC_PR_MARGIN_BOTTOM,
    IDC_PR_GROUP_TOP,
    IDC_PR_GROUP_PAGES,
    IDC_PR_GROUP_SIZING,
    IDC_PR_GROUP_SIDES,
    IDC_PR_GROUP_COMMENTS,
    IDC_PR_GROUP_PREVIEW,
};

// Multiple: the "Pages per sheet" presets, the last one is Custom
static const struct {
    int cols, rows;
} kPerSheetPresets[] = {{2, 1}, {2, 2}, {3, 2}, {3, 3}, {4, 4}};
constexpr int kPerSheetCustomIdx = dimof(kPerSheetPresets);
constexpr int kMaxGridSide = 10;

// Comments & Forms, in the combo's order
static const PrintAnnots kCommentsModes[] = {PrintAnnots::Document, PrintAnnots::All, PrintAnnots::Stamps,
                                             PrintAnnots::FormsOnly};

// the controls of each Page Sizing & Handling button
static const int kSizeCtrls[] = {IDC_PR_FIT,           IDC_PR_ACTUAL,         IDC_PR_SHRINK,
                                 IDC_PR_CUSTOM,        IDC_PR_CUSTOM_EDIT,    IDC_PR_CUSTOM_PERCENT,
                                 IDC_PR_ROTATE_LABEL,  IDC_PR_ROTATE,         IDC_PR_POSITION_LABEL,
                                 IDC_PR_POSITION,      IDC_PR_OFFSET_LABEL,   IDC_PR_OFFSET_X,
                                 IDC_PR_OFFSET_X_UNIT, IDC_PR_OFFSET_Y_LABEL, IDC_PR_OFFSET_Y,
                                 IDC_PR_OFFSET_UNIT};
static const int kPosterCtrls[] = {IDC_PR_TILE_LABEL,    IDC_PR_TILE_EDIT,    IDC_PR_TILE_PERCENT,
                                   IDC_PR_OVERLAP_LABEL, IDC_PR_OVERLAP_EDIT, IDC_PR_OVERLAP_UNIT,
                                   IDC_PR_CUT_MARKS,     IDC_PR_LABELS,       IDC_PR_LARGE_ONLY};
static const int kMultipleCtrls[] = {IDC_PR_PER_SHEET_LABEL, IDC_PR_PER_SHEET,   IDC_PR_COLS_EDIT, IDC_PR_BY_LABEL,
                                     IDC_PR_ROWS_EDIT,       IDC_PR_ORDER_LABEL, IDC_PR_ORDER,     IDC_PR_BORDER};
static const int kBookletCtrls[] = {IDC_PR_SUBSET_LABEL,    IDC_PR_SUBSET,    IDC_PR_SHEETS_LABEL,  IDC_PR_SHEETS_FROM,
                                    IDC_PR_SHEETS_TO_LABEL, IDC_PR_SHEETS_TO, IDC_PR_BINDING_LABEL, IDC_PR_BINDING};

/*
 * The controls, created in this order (the tab order) when the dialog opens.
 * The "&" marks the access key; "&&" is a literal "&". TrN() marks the text
 * for translation, Tr() translates it when the control is created.
 */
struct CtrlDef {
    int id;
    const WCHAR* cls;
    Str text;
    DWORD style;
};

static const CtrlDef kPrintCtrls[] = {
    {IDC_PR_GROUP_TOP, L"BUTTON", StrL(""), BS_GROUPBOX},
    {IDC_PR_PRINTER_LABEL, L"STATIC", TrN("Pri&nter:"), SS_LEFT},
    {IDC_PR_PRINTER, L"COMBOBOX", StrL(""), CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_GROUP},
    {IDC_PR_PROPERTIES, L"BUTTON", TrN("&Properties"), BS_PUSHBUTTON | WS_TABSTOP},
    {IDC_PR_HELP, L"SysLink", StrL("<a>Help</a>"), WS_TABSTOP}, // text set in InitControls
    {IDC_PR_HELP_ICON, L"STATIC", StrL(""), SS_OWNERDRAW | SS_NOTIFY},
    {IDC_PR_COPIES_LABEL, L"STATIC", TrN("&Copies:"), SS_LEFT},
    {IDC_PR_COPIES, L"EDIT", StrL(""), ES_NUMBER | WS_TABSTOP},
    {IDC_PR_COPIES_SPIN, L"msctls_updown32", StrL(""),
     UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_AUTOBUDDY | UDS_ARROWKEYS | UDS_NOTHOUSANDS},
    {IDC_PR_COLLATE, L"BUTTON", TrN("Collate"), BS_AUTOCHECKBOX | WS_TABSTOP},
    {IDC_PR_TO_FILE, L"BUTTON", TrN("Print to &file"), BS_AUTOCHECKBOX | WS_TABSTOP},
    {IDC_PR_GRAYSCALE, L"BUTTON", TrN("Print in gra&yscale (black and white)"), BS_AUTOCHECKBOX | WS_TABSTOP},
    {IDC_PR_SAVE_INK, L"BUTTON", TrN("Save ink/toner"), BS_AUTOCHECKBOX | WS_TABSTOP},
    {IDC_PR_GROUP_PAGES, L"BUTTON", StrL(""), BS_GROUPBOX},
    {IDC_PR_PAGES_TITLE, L"STATIC", TrN("Pages to Print"), SS_LEFT},
    {IDC_PR_ALL, L"BUTTON", TrN("&All"), BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP},
    {IDC_PR_CURRENT, L"BUTTON", TrN("Current"), BS_AUTORADIOBUTTON},
    {IDC_PR_PAGES, L"BUTTON", TrN("Pages"), BS_AUTORADIOBUTTON},
    {IDC_PR_CURRENT_VIEW, L"BUTTON", TrN("Current &view"), BS_AUTORADIOBUTTON},
    {IDC_PR_PAGES_EDIT, L"EDIT", StrL(""), ES_AUTOHSCROLL | WS_GROUP | WS_TABSTOP},
    {IDC_PR_MORE_ARROW, L"STATIC", StrL(""), SS_NOTIFY},
    {IDC_PR_MORE, L"STATIC", TrN("More Options"), SS_NOTIFY},
    {IDC_PR_REVERSE, L"BUTTON", TrN("Reverse pages"), BS_AUTOCHECKBOX | WS_TABSTOP},
    {IDC_PR_ODD_EVEN_LABEL, L"STATIC", TrN("Odd or Even Pages:"), SS_LEFT},
    {IDC_PR_ODD_EVEN, L"COMBOBOX", StrL(""), CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP},
    {IDC_PR_GROUP_SIZING, L"BUTTON", StrL(""), BS_GROUPBOX},
    {IDC_PR_SIZING_TITLE, L"STATIC", TrN("Page Sizing && Handling"), SS_LEFT},
    {IDC_PR_SIZE, L"BUTTON", TrN("S&ize"), BS_AUTORADIOBUTTON | BS_PUSHLIKE | WS_GROUP | WS_TABSTOP},
    {IDC_PR_POSTER, L"BUTTON", TrN("Poster"), BS_AUTORADIOBUTTON | BS_PUSHLIKE},
    {IDC_PR_MULTIPLE, L"BUTTON", TrN("Multiple"), BS_AUTORADIOBUTTON | BS_PUSHLIKE},
    {IDC_PR_BOOKLET, L"BUTTON", TrN("Booklet"), BS_AUTORADIOBUTTON | BS_PUSHLIKE},
    {IDC_PR_FIT, L"BUTTON", TrN("&Fit"), BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP},
    {IDC_PR_ACTUAL, L"BUTTON", TrN("Actual size"), BS_AUTORADIOBUTTON},
    {IDC_PR_SHRINK, L"BUTTON", TrN("Shrink oversized pages"), BS_AUTORADIOBUTTON},
    {IDC_PR_CUSTOM, L"BUTTON", TrN("Custom Scale:"), BS_AUTORADIOBUTTON},
    {IDC_PR_CUSTOM_EDIT, L"EDIT", StrL(""), ES_AUTOHSCROLL | WS_GROUP | WS_TABSTOP},
    {IDC_PR_CUSTOM_PERCENT, L"STATIC", StrL("%"), SS_LEFT},
    {IDC_PR_ROTATE_LABEL, L"STATIC", TrN("Rotate:"), SS_LEFT},
    {IDC_PR_ROTATE, L"COMBOBOX", StrL(""), CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP},
    {IDC_PR_POSITION_LABEL, L"STATIC", TrN("Position:"), SS_LEFT},
    {IDC_PR_POSITION, L"COMBOBOX", StrL(""), CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP},
    {IDC_PR_OFFSET_LABEL, L"STATIC", TrN("Offset X:"), SS_LEFT},
    {IDC_PR_OFFSET_X, L"EDIT", StrL(""), ES_AUTOHSCROLL | WS_TABSTOP},
    {IDC_PR_OFFSET_X_UNIT, L"STATIC", StrL(""), SS_LEFT},
    {IDC_PR_OFFSET_Y_LABEL, L"STATIC", TrN("Offset Y:"), SS_LEFT},
    {IDC_PR_OFFSET_Y, L"EDIT", StrL(""), ES_AUTOHSCROLL | WS_TABSTOP},
    {IDC_PR_OFFSET_UNIT, L"STATIC", StrL(""), SS_LEFT},
    {IDC_PR_TILE_LABEL, L"STATIC", TrN("Tile Scale:"), SS_LEFT},
    {IDC_PR_TILE_EDIT, L"EDIT", StrL(""), ES_AUTOHSCROLL | WS_TABSTOP},
    {IDC_PR_TILE_PERCENT, L"STATIC", StrL("%"), SS_LEFT},
    {IDC_PR_OVERLAP_LABEL, L"STATIC", TrN("Overlap:"), SS_LEFT},
    {IDC_PR_OVERLAP_EDIT, L"EDIT", StrL(""), ES_AUTOHSCROLL | WS_TABSTOP},
    {IDC_PR_OVERLAP_UNIT, L"STATIC", StrL(""), SS_LEFT},
    {IDC_PR_CUT_MARKS, L"BUTTON", TrN("Cut mar&ks"), BS_AUTOCHECKBOX | WS_TABSTOP},
    {IDC_PR_LABELS, L"BUTTON", TrN("Labels"), BS_AUTOCHECKBOX | WS_TABSTOP},
    {IDC_PR_LARGE_ONLY, L"BUTTON", TrN("Tile only large pages"), BS_AUTOCHECKBOX | WS_TABSTOP},
    {IDC_PR_PER_SHEET_LABEL, L"STATIC", TrN("Pages per sheet:"), SS_LEFT},
    {IDC_PR_PER_SHEET, L"COMBOBOX", StrL(""), CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP},
    {IDC_PR_COLS_EDIT, L"EDIT", StrL(""), ES_NUMBER | WS_TABSTOP},
    {IDC_PR_BY_LABEL, L"STATIC", TrN("by"), SS_LEFT},
    {IDC_PR_ROWS_EDIT, L"EDIT", StrL(""), ES_NUMBER | WS_TABSTOP},
    {IDC_PR_ORDER_LABEL, L"STATIC", TrN("Page order:"), SS_LEFT},
    {IDC_PR_ORDER, L"COMBOBOX", StrL(""), CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP},
    {IDC_PR_BORDER, L"BUTTON", TrN("Print page border"), BS_AUTOCHECKBOX | WS_TABSTOP},
    {IDC_PR_SUBSET_LABEL, L"STATIC", TrN("Booklet subset:"), SS_LEFT},
    {IDC_PR_SUBSET, L"COMBOBOX", StrL(""), CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP},
    {IDC_PR_SHEETS_LABEL, L"STATIC", TrN("Sheets from"), SS_LEFT},
    {IDC_PR_SHEETS_FROM, L"EDIT", StrL(""), ES_NUMBER | WS_TABSTOP},
    {IDC_PR_SHEETS_TO_LABEL, L"STATIC", TrN(" to "), SS_LEFT},
    {IDC_PR_SHEETS_TO, L"EDIT", StrL(""), ES_NUMBER | WS_TABSTOP},
    {IDC_PR_BINDING_LABEL, L"STATIC", TrN("Binding:"), SS_LEFT},
    {IDC_PR_BINDING, L"COMBOBOX", StrL(""), CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP},
    {IDC_PR_GROUP_SIDES, L"BUTTON", StrL(""), BS_GROUPBOX},
    {IDC_PR_DUPLEX, L"BUTTON", TrN("Print on &both sides of paper"), BS_AUTOCHECKBOX | WS_TABSTOP | WS_GROUP},
    {IDC_PR_FLIP_LONG, L"BUTTON", TrN("Flip on &long edge"), BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP},
    {IDC_PR_FLIP_SHORT, L"BUTTON", TrN("Flip on short edge"), BS_AUTORADIOBUTTON},
    {IDC_PR_ORIENT_LABEL, L"STATIC", TrN("Orientation:"), SS_LEFT | WS_GROUP},
    {IDC_PR_ORIENT_AUTO, L"BUTTON", TrN("Auto "), BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP},
    {IDC_PR_ORIENT_PORTRAIT, L"BUTTON", TrN("Portrait"), BS_AUTORADIOBUTTON},
    {IDC_PR_ORIENT_LANDSCAPE, L"BUTTON", TrN("Landscape"), BS_AUTORADIOBUTTON},
    {IDC_PR_AUTO_ROTATE, L"BUTTON", TrN("Auto-rotate pages within each sheet"),
     BS_AUTOCHECKBOX | WS_TABSTOP | WS_GROUP},
    {IDC_PR_GROUP_COMMENTS, L"BUTTON", StrL(""), BS_GROUPBOX},
    {IDC_PR_COMMENTS_TITLE, L"STATIC", TrN("Co&mments && Forms"), SS_LEFT},
    {IDC_PR_COMMENTS, L"COMBOBOX", StrL(""), CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_GROUP},
    {IDC_PR_GROUP_PREVIEW, L"BUTTON", StrL(""), BS_GROUPBOX},
    {IDC_PR_SCALE_LABEL, L"STATIC", StrL(""), SS_LEFT},
    {IDC_PR_PREVIEW, L"STATIC", StrL(""), SS_OWNERDRAW},
    {IDC_PR_PREV, L"BUTTON", StrL("<"), BS_PUSHBUTTON | WS_TABSTOP},
    {IDC_PR_SLIDER, L"msctls_trackbar32", StrL(""), TBS_HORZ | TBS_NOTICKS | WS_TABSTOP},
    {IDC_PR_NEXT, L"BUTTON", StrL(">"), BS_PUSHBUTTON | WS_TABSTOP},
    {IDC_PR_SHEET_LABEL, L"STATIC", StrL(""), SS_CENTER},
    {IDC_PR_GROUP_PAPER, L"BUTTON", StrL(""), BS_GROUPBOX},
    {IDC_PR_PAPER_TITLE, L"STATIC", TrN("Paper"), SS_LEFT},
    {IDC_PR_PAPER_SIZE_LABEL, L"STATIC", TrN("Si&ze:"), SS_LEFT},
    {IDC_PR_PAPER_SIZE, L"COMBOBOX", StrL(""), CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP},
    {IDC_PR_SOURCE_LABEL, L"STATIC", TrN("Sou&rce:"), SS_LEFT},
    {IDC_PR_SOURCE, L"COMBOBOX", StrL(""), CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP},
    {IDC_PR_QUALITY_LABEL, L"STATIC", TrN("&Quality:"), SS_LEFT},
    {IDC_PR_QUALITY, L"COMBOBOX", StrL(""), CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP},
    {IDC_PR_MARGINS_LABEL, L"STATIC", StrL(""), SS_LEFT},
    {IDC_PR_MARGIN_LEFT_LABEL, L"STATIC", TrN("Left:"), SS_LEFT},
    {IDC_PR_MARGIN_LEFT, L"EDIT", StrL(""), ES_AUTOHSCROLL | WS_TABSTOP},
    {IDC_PR_MARGIN_RIGHT_LABEL, L"STATIC", TrN("Right:"), SS_LEFT},
    {IDC_PR_MARGIN_RIGHT, L"EDIT", StrL(""), ES_AUTOHSCROLL | WS_TABSTOP},
    {IDC_PR_MARGIN_TOP_LABEL, L"STATIC", TrN("Top:"), SS_LEFT},
    {IDC_PR_MARGIN_TOP, L"EDIT", StrL(""), ES_AUTOHSCROLL | WS_TABSTOP},
    {IDC_PR_MARGIN_BOTTOM_LABEL, L"STATIC", TrN("Bottom:"), SS_LEFT},
    {IDC_PR_MARGIN_BOTTOM, L"EDIT", StrL(""), ES_AUTOHSCROLL | WS_TABSTOP},
    {IDOK, L"BUTTON", TrN("Print"), BS_DEFPUSHBUTTON | WS_TABSTOP},
    {IDCANCEL, L"BUTTON", TrN("Cancel"), BS_PUSHBUTTON | WS_TABSTOP},
};

// creates the controls of an empty dialog, in its font
static void CreateDlgControls(HWND hDlg, const CtrlDef* defs, int n) {
    HFONT font = (HFONT)SendMessageW(hDlg, WM_GETFONT, 0, 0);
    for (int i = 0; i < n; i++) {
        const CtrlDef& def = defs[i];
        DWORD exStyle = wcscmp(def.cls, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0;
        DWORD style = WS_CHILD | WS_VISIBLE | def.style;
        Str text = len(def.text) > 0 ? Tr(def.text) : def.text;
        HWND hwnd = CreateWindowExW(exStyle, def.cls, CWStrTemp(text), style, 0, 0, 10, 10, hDlg,
                                    (HMENU)(INT_PTR)def.id, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, FALSE);
    }
}

/*
 * An empty dialog template (DLGTEMPLATEEX) with the title and the
 * Segoe UI 9 font: the controls are created in WM_INITDIALOG.
 */
static DLGTEMPLATE* NewDlgTemplate(const WCHAR* title) {
    Vec<WORD> t;
    auto addDword = [&t](DWORD v) {
        VecAppend(t, LOWORD(v));
        VecAppend(t, HIWORD(v));
    };
    auto addWStr = [&t](const WCHAR* s) {
        for (; *s; s++) {
            VecAppend(t, (WORD)*s);
        }
        VecAppend(t, (WORD)0);
    };
    VecAppend(t, (WORD)1);      // dlgVer
    VecAppend(t, (WORD)0xFFFF); // signature
    addDword(0);                // helpID
    addDword(0);                // exStyle
    addDword(DS_SETFONT | DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU);
    VecAppend(t, (WORD)0); // no items
    for (int i = 0; i < 4; i++) {
        VecAppend(t, (WORD)0); // x, y, cx, cy: LayoutDialog sizes it
    }
    VecAppend(t, (WORD)0); // no menu
    VecAppend(t, (WORD)0); // default class
    addWStr(title);
    VecAppend(t, (WORD)9);     // point size
    VecAppend(t, (WORD)400);   // weight
    VecAppend(t, (WORD)0x100); // italic = 0, charset = 1 (default)
    addWStr(L"Segoe UI");
    return (DLGTEMPLATE*)MemDup(nullptr, t.els, (size_t)len(t) * sizeof(WORD));
}

/*
 * Positions in pixels at 96 dpi, with every optional row shown. A band is a
 * strip of the left column that is hidden as a whole: what is below it moves
 * up by its height and the group boxes around it shrink.
 */
struct CtrlPos {
    int id;
    int x, y, dx, dy;
};

static const CtrlPos kLayout[] = {
    {IDC_PR_GROUP_TOP, 12, 4, 758, 78},
    {IDC_PR_PRINTER_LABEL, 27, 19, 46, 15},
    {IDC_PR_COPIES_LABEL, 27, 53, 46, 15},
    {IDC_PR_PRINTER, 76, 13, 252, 200},
    {IDC_PR_PROPERTIES, 332, 13, 86, 26},
    {IDC_PR_HELP, 711, 18, 26, 16},
    {IDC_PR_HELP_ICON, 739, 18, 16, 16},
    {IDC_PR_COPIES, 76, 49, 40, 23},
    {IDC_PR_COPIES_SPIN, 117, 49, 17, 23},
    {IDC_PR_COLLATE, 140, 52, 80, 18},
    {IDC_PR_TO_FILE, 230, 52, 105, 18},
    {IDC_PR_GRAYSCALE, 341, 52, 212, 18},
    {IDC_PR_SAVE_INK, 565, 52, 109, 18},

    {IDC_PR_GROUP_PAGES, 12, 84, 442, 136},
    {IDC_PR_PAGES_TITLE, 27, 99, 150, 15},
    {IDC_PR_ALL, 27, 121, 51, 18},
    {IDC_PR_CURRENT, 103, 121, 75, 18},
    {IDC_PR_PAGES, 192, 121, 67, 18},
    {IDC_PR_PAGES_EDIT, 263, 118, 150, 23},
    {IDC_PR_MORE_ARROW, 27, 145, 16, 16},
    {IDC_PR_MORE, 43, 145, 90, 15},
    {IDC_PR_CURRENT_VIEW, 48, 168, 101, 18},
    {IDC_PR_REVERSE, 153, 168, 120, 18},
    {IDC_PR_ODD_EVEN_LABEL, 48, 194, 122, 15},
    {IDC_PR_ODD_EVEN, 172, 189, 179, 200},

    {IDC_PR_GROUP_SIZING, 12, 222, 442, 214},
    {IDC_PR_SIZING_TITLE, 27, 239, 165, 15},
    {IDC_PR_SIZE, 27, 259, 100, 32},
    {IDC_PR_POSTER, 131, 259, 100, 32},
    {IDC_PR_MULTIPLE, 235, 259, 100, 32},
    {IDC_PR_BOOKLET, 339, 259, 100, 32},

    {IDC_PR_FIT, 27, 302, 150, 18},
    {IDC_PR_ACTUAL, 181, 302, 150, 18},
    {IDC_PR_SHRINK, 27, 328, 150, 18},
    {IDC_PR_CUSTOM, 181, 328, 108, 18},
    {IDC_PR_CUSTOM_EDIT, 293, 325, 58, 23},
    {IDC_PR_CUSTOM_PERCENT, 355, 329, 12, 15},
    {IDC_PR_ROTATE_LABEL, 27, 360, 56, 15},
    {IDC_PR_ROTATE, 86, 354, 85, 200},
    {IDC_PR_POSITION_LABEL, 181, 360, 64, 15},
    {IDC_PR_POSITION, 248, 354, 120, 200},
    {IDC_PR_OFFSET_LABEL, 27, 396, 56, 15},
    {IDC_PR_OFFSET_X, 86, 392, 50, 23},
    {IDC_PR_OFFSET_X_UNIT, 141, 396, 26, 15},
    {IDC_PR_OFFSET_Y_LABEL, 181, 396, 64, 15},
    {IDC_PR_OFFSET_Y, 248, 392, 50, 23},
    {IDC_PR_OFFSET_UNIT, 303, 396, 26, 15},

    {IDC_PR_TILE_LABEL, 27, 303, 52, 15},
    {IDC_PR_TILE_EDIT, 81, 299, 46, 23},
    {IDC_PR_TILE_PERCENT, 131, 303, 12, 15},
    {IDC_PR_OVERLAP_LABEL, 171, 303, 48, 15},
    {IDC_PR_OVERLAP_EDIT, 222, 299, 46, 23},
    {IDC_PR_OVERLAP_UNIT, 272, 303, 26, 15},
    {IDC_PR_CUT_MARKS, 27, 328, 120, 18},
    {IDC_PR_LABELS, 171, 328, 100, 18},
    {IDC_PR_LARGE_ONLY, 27, 352, 160, 18},

    {IDC_PR_PER_SHEET_LABEL, 27, 302, 100, 15},
    {IDC_PR_PER_SHEET, 128, 297, 98, 200},
    {IDC_PR_COLS_EDIT, 230, 298, 30, 23},
    {IDC_PR_BY_LABEL, 264, 302, 16, 15},
    {IDC_PR_ROWS_EDIT, 282, 298, 30, 23},
    {IDC_PR_ORDER_LABEL, 27, 331, 100, 15},
    {IDC_PR_ORDER, 128, 326, 193, 200},
    {IDC_PR_BORDER, 27, 357, 160, 18},

    {IDC_PR_SUBSET_LABEL, 27, 302, 100, 15},
    {IDC_PR_SUBSET, 128, 297, 208, 200},
    {IDC_PR_SHEETS_LABEL, 27, 331, 100, 15},
    {IDC_PR_SHEETS_FROM, 128, 327, 54, 23},
    {IDC_PR_SHEETS_TO_LABEL, 186, 331, 18, 15},
    {IDC_PR_SHEETS_TO, 206, 327, 54, 23},
    {IDC_PR_BINDING_LABEL, 27, 359, 100, 15},
    {IDC_PR_BINDING, 128, 354, 110, 200},

    {IDC_PR_GROUP_SIDES, 12, 438, 442, 131},
    {IDC_PR_DUPLEX, 27, 453, 200, 18},
    {IDC_PR_FLIP_LONG, 43, 475, 122, 18},
    {IDC_PR_FLIP_SHORT, 169, 475, 127, 18},
    {IDC_PR_ORIENT_LABEL, 27, 501, 80, 15},
    {IDC_PR_ORIENT_AUTO, 27, 520, 64, 18},
    {IDC_PR_ORIENT_PORTRAIT, 105, 520, 76, 18},
    {IDC_PR_ORIENT_LANDSCAPE, 195, 520, 90, 18},
    {IDC_PR_AUTO_ROTATE, 27, 542, 260, 18},

    {IDC_PR_GROUP_COMMENTS, 12, 571, 442, 78},
    {IDC_PR_COMMENTS_TITLE, 27, 586, 150, 15},
    {IDC_PR_COMMENTS, 27, 603, 234, 200},

    {IDC_PR_GROUP_PREVIEW, 468, 84, 302, 340},
    {IDC_PR_SCALE_LABEL, 483, 99, 272, 15},
    {IDC_PR_PREVIEW, 483, 118, 272, 244},
    {IDC_PR_PREV, 483, 368, 32, 26},
    {IDC_PR_SLIDER, 532, 369, 173, 23},
    {IDC_PR_NEXT, 723, 368, 32, 26},
    {IDC_PR_SHEET_LABEL, 519, 398, 200, 15},

    {IDC_PR_GROUP_PAPER, 468, 426, 302, 215},
    {IDC_PR_PAPER_TITLE, 483, 441, 150, 15},
    {IDC_PR_PAPER_SIZE_LABEL, 483, 465, 54, 15},
    {IDC_PR_PAPER_SIZE, 540, 459, 215, 300},
    {IDC_PR_SOURCE_LABEL, 483, 495, 54, 15},
    {IDC_PR_SOURCE, 540, 489, 215, 200},
    {IDC_PR_QUALITY_LABEL, 483, 525, 54, 15},
    {IDC_PR_QUALITY, 540, 519, 215, 200},
    {IDC_PR_MARGINS_LABEL, 483, 553, 150, 15},
    {IDC_PR_MARGIN_LEFT_LABEL, 483, 578, 50, 15},
    {IDC_PR_MARGIN_LEFT, 540, 574, 50, 23},
    {IDC_PR_MARGIN_RIGHT_LABEL, 645, 578, 56, 15},
    {IDC_PR_MARGIN_RIGHT, 705, 574, 50, 23},
    {IDC_PR_MARGIN_TOP_LABEL, 483, 608, 50, 15},
    {IDC_PR_MARGIN_TOP, 540, 604, 50, 23},
    {IDC_PR_MARGIN_BOTTOM_LABEL, 645, 608, 56, 15},
    {IDC_PR_MARGIN_BOTTOM, 705, 604, 50, 23},
};

struct Band {
    int y, dy;
};
static const Band kBandMore = {163, 47};
static const Band kBandSizeExtra = {380, 49};
static const Band kBandDuplex = {446, 47};
static const Band kBandAutoRotate = {539, 22};
constexpr int kLeftColumnDx = 460;
constexpr int kRightColumnBottom = 641;
constexpr int kDlgDx = 782;
constexpr int kButtonDy = 26;
// the row a combo's y is the top of: its center is half this below
constexpr int kComboRowDy = 26;

// Marlett font glyphs for the More Options arrow
static const WCHAR* kArrowRight = L"4";
static const WCHAR* kArrowDown = L"6";

// remembered for the next dialog in this process
static PrintJobOptions gLastJob;
static bool gHasLastJob = false;
static Str gLastPrinterName;
static DEVMODEW* gLastDevMode = nullptr;
static bool gLastMoreOptions = false;

struct PrintDlgState {
    MainWindow* win = nullptr;
    DisplayModel* dm = nullptr;
    EngineBase* engine = nullptr;
    int nPages = 0;
    int curPageNo = 1;
    HWND hwnd = nullptr;

    StrVec printerNames;
    Vec<Point> resolutions;      // the printer's print qualities, in dpi
    Printer* printer = nullptr;  // capabilities of the selected printer
    DEVMODEW* devMode = nullptr; // edited by Properties and the Paper section

    Print_Advanced_Data adv;
    PrintJobOptions opts;
    PagesToPrint pagesMode = PagesToPrint::All;
    bool moreOptions = false;
    bool printToFile = false;
    bool inches = false;        // lengths shown in inches (US), else mm
    int layoutKey = -1;         // what LayoutDialog() last laid out for
    Vec<PRINTPAGERANGE> ranges; // set by Print
    Str outputFile;             // set by Print when printing to a file

    PrintGeometry geo;
    PrintPlan plan;
    // preview: a sheet, or for Poster all the tiles of one page
    int previewIdx = 0;
    HBITMAP previewBmp = nullptr;

    HFONT boldFont = nullptr;
    HFONT arrowFont = nullptr;
    bool updating = false;

    ~PrintDlgState() {
        delete printer;
        free(devMode);
        str::Free(outputFile);
        if (previewBmp) {
            DeleteObject(previewBmp);
        }
        if (boldFont) {
            DeleteObject(boldFont);
        }
        if (arrowFont) {
            DeleteObject(arrowFont);
        }
    }
};

static DEVMODEW* DupDevMode(const DEVMODEW* dm) {
    if (!dm) {
        return nullptr;
    }
    return (DEVMODEW*)MemDup(nullptr, dm, (size_t)dm->dmSize + dm->dmDriverExtra);
}

static HWND Item(PrintDlgState* d, int id) {
    return GetDlgItem(d->hwnd, id);
}

static bool IsChecked(PrintDlgState* d, int id) {
    return IsDlgButtonChecked(d->hwnd, id) == BST_CHECKED;
}

static void SetChecked(PrintDlgState* d, int id, bool checked) {
    CheckDlgButton(d->hwnd, id, checked ? BST_CHECKED : BST_UNCHECKED);
}

static void EnableItem(PrintDlgState* d, int id, bool enable) {
    EnableWindow(Item(d, id), enable);
}

static float GetItemFloat(PrintDlgState* d, int id, float fallback) {
    TempStr s = str::DupTemp(HwndGetTextTemp(Item(d, id)));
    if (len(s) == 0) {
        return fallback;
    }
    // "0,005" as typed in locales with a decimal comma
    str::TransCharsInPlace(s, StrL(","), StrL("."));
    char* end = nullptr;
    const char* cs = CStrTemp(s);
    float v = strtof(cs, &end);
    return end == cs ? fallback : v;
}

static void SetItemFloat(PrintDlgState* d, int id, float v) {
    HwndSetText(Item(d, id), fmt("%g", v));
}

// lengths are kept in mm and shown in the user's unit: mm, or inches in the US
static Str LengthUnit(PrintDlgState* d) {
    return d->inches ? Tr("in") : Tr("mm");
}

static float GetItemLength(PrintDlgState* d, int id, float fallbackMm) {
    float v = GetItemFloat(d, id, NAN);
    if (isnan(v)) {
        return fallbackMm;
    }
    return d->inches ? v * 25.4f : v;
}

static void SetItemLength(PrintDlgState* d, int id, float mm) {
    float v = d->inches ? mm / 25.4f : mm;
    SetItemFloat(d, id, roundf(v * 100) / 100);
}

static int GetItemInt(PrintDlgState* d, int id, int fallback) {
    BOOL ok = FALSE;
    int v = (int)GetDlgItemInt(d->hwnd, id, &ok, FALSE);
    return ok ? v : fallback;
}

static int ComboIdx(PrintDlgState* d, int id) {
    return std::max(0, CbGetCurrentSelection(Item(d, id)));
}

static void FillCombo(PrintDlgState* d, int id, const Str* items, int n, int sel) {
    HWND hwnd = Item(d, id);
    for (int i = 0; i < n; i++) {
        CbAddString(hwnd, items[i]);
    }
    CbSetCurrentSelection(hwnd, sel);
}

static bool Contains(const int* ids, int n, int id) {
    for (int i = 0; i < n; i++) {
        if (ids[i] == id) {
            return true;
        }
    }
    return false;
}

static PrintSizing CheckedSizing(PrintDlgState* d) {
    if (IsChecked(d, IDC_PR_POSTER)) {
        return PrintSizing::Poster;
    }
    if (IsChecked(d, IDC_PR_MULTIPLE)) {
        return PrintSizing::Multiple;
    }
    if (IsChecked(d, IDC_PR_BOOKLET)) {
        return PrintSizing::Booklet;
    }
    return PrintSizing::Size;
}

// two-sided printing is chosen for Size and Multiple; a booklet sets its own
static bool ShowsDuplex(PrintSizing sizing) {
    return sizing == PrintSizing::Size || sizing == PrintSizing::Multiple;
}

static bool ShowsAutoRotate(PrintSizing sizing) {
    return sizing == PrintSizing::Multiple || sizing == PrintSizing::Booklet;
}

static bool IsItemShown(PrintDlgState* d, PrintSizing sizing, int id) {
    if (Contains(kSizeCtrls, dimof(kSizeCtrls), id)) {
        return sizing == PrintSizing::Size;
    }
    if (Contains(kPosterCtrls, dimof(kPosterCtrls), id)) {
        return sizing == PrintSizing::Poster;
    }
    if (Contains(kMultipleCtrls, dimof(kMultipleCtrls), id)) {
        return sizing == PrintSizing::Multiple;
    }
    if (Contains(kBookletCtrls, dimof(kBookletCtrls), id)) {
        return sizing == PrintSizing::Booklet;
    }
    switch (id) {
        case IDC_PR_COLLATE:
            return GetItemInt(d, IDC_PR_COPIES, 1) > 1;
        case IDC_PR_ORIENT_AUTO:
            return sizing == PrintSizing::Size;
    }
    return true;
}

/*
 * Moves every control to its place for the current choices and sizes the
 * dialog to fit. Rows of hidden bands are hidden, rows below move up.
 */
static void LayoutDialog(PrintDlgState* d) {
    PrintSizing sizing = CheckedSizing(d);
    int dpi = DpiGetForHwnd(d->hwnd);
    // re-laying out repaints everything: only when something moves
    bool showsCollate = IsItemShown(d, sizing, IDC_PR_COLLATE);
    int layoutKey = ((((int)sizing * 2 + (d->moreOptions ? 1 : 0)) * 2 + (showsCollate ? 1 : 0)) * 1000) + dpi;
    if (layoutKey == d->layoutKey) {
        return;
    }
    d->layoutKey = layoutKey;
    // WM_SETREDRAW TRUE also shows a window: not while the dialog is being created
    bool isVisible = IsWindowVisible(d->hwnd);
    if (isVisible) {
        SendMessageW(d->hwnd, WM_SETREDRAW, FALSE, 0);
    }

    Band hidden[4];
    int nHidden = 0;
    if (!d->moreOptions) {
        hidden[nHidden++] = kBandMore;
    }
    if (sizing != PrintSizing::Size) {
        hidden[nHidden++] = kBandSizeExtra;
    }
    if (!ShowsDuplex(sizing)) {
        hidden[nHidden++] = kBandDuplex;
    }
    if (!ShowsAutoRotate(sizing)) {
        hidden[nHidden++] = kBandAutoRotate;
    }

    auto scale = [dpi](int v) { return DpiScaleByDpi(dpi, v); };
    HDWP hdwp = BeginDeferWindowPos(dimof(kLayout) + 3);
    int leftBottom = 0;
    // a combo's height comes from its font, so it is centered on its row afterwards
    struct ComboRow {
        HWND hwnd;
        int centerY;
    };
    Vec<ComboRow> combos;
    for (const CtrlPos& pos : kLayout) {
        HWND hwnd = Item(d, pos.id);
        if (!hwnd) {
            continue;
        }
        int y = pos.y;
        int dy = pos.dy;
        bool show = IsItemShown(d, sizing, pos.id);
        bool isGroup = str::Eq(HwndGetClassName(hwnd), StrL("Button")) &&
                       (GetWindowLongW(hwnd, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX;
        if (pos.x < kLeftColumnDx) {
            for (int i = 0; i < nHidden; i++) {
                Band b = hidden[i];
                if (pos.y >= b.y && pos.y < b.y + b.dy && !isGroup) {
                    show = false;
                } else if (pos.y >= b.y + b.dy) {
                    y -= b.dy;
                } else if (isGroup && pos.y < b.y && pos.y + pos.dy >= b.y + b.dy) {
                    dy -= b.dy;
                }
            }
            if (isGroup) {
                leftBottom = std::max(leftBottom, y + dy);
            }
        }
        // without Auto, Portrait and Landscape move left
        int x = pos.x;
        if (sizing != PrintSizing::Size && pos.id == IDC_PR_ORIENT_PORTRAIT) {
            x = 27;
        } else if (sizing != PrintSizing::Size && pos.id == IDC_PR_ORIENT_LANDSCAPE) {
            x = 117;
        }
        if (show != ((GetWindowLongW(hwnd, GWL_STYLE) & WS_VISIBLE) != 0)) {
            ShowWindow(hwnd, show ? SW_SHOW : SW_HIDE);
        }
        if (str::Eq(HwndGetClassName(hwnd), StrL("ComboBox"))) {
            VecAppend(combos, ComboRow{hwnd, scale(y + kComboRowDy / 2)});
        }
        hdwp = DeferWindowPos(hdwp, hwnd, nullptr, scale(x), scale(y), scale(pos.dx), scale(dy),
                              SWP_NOZORDER | SWP_NOACTIVATE);
    }

    int columnsBottom = std::max(leftBottom, kRightColumnBottom);
    int buttonsY = columnsBottom + 12;
    struct {
        int id, x;
    } buttons[] = {{IDOK, 586}, {IDCANCEL, 682}};
    for (auto& b : buttons) {
        hdwp = DeferWindowPos(hdwp, Item(d, b.id), nullptr, scale(b.x), scale(buttonsY), scale(86), scale(kButtonDy),
                              SWP_NOZORDER | SWP_NOACTIVATE);
    }
    EndDeferWindowPos(hdwp);
    // the last section of each column reaches down to the same line
    int lastGroups[] = {IDC_PR_GROUP_COMMENTS, IDC_PR_GROUP_PAPER};
    for (int id : lastGroups) {
        HWND hwnd = Item(d, id);
        RECT r;
        GetWindowRect(hwnd, &r);
        MapWindowPoints(HWND_DESKTOP, d->hwnd, (POINT*)&r, 2);
        SetWindowPos(hwnd, nullptr, 0, 0, r.right - r.left, scale(columnsBottom) - r.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    for (ComboRow& c : combos) {
        RECT r;
        GetWindowRect(c.hwnd, &r);
        MapWindowPoints(HWND_DESKTOP, d->hwnd, (POINT*)&r, 2);
        int dy = r.bottom - r.top;
        SetWindowPos(c.hwnd, nullptr, r.left, c.centerY - dy / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    SetWindowTextW(Item(d, IDC_PR_MORE_ARROW), d->moreOptions ? kArrowDown : kArrowRight);

    RECT rc{0, 0, scale(kDlgDx), scale(buttonsY + kButtonDy + 12)};
    AdjustWindowRectEx(&rc, (DWORD)GetWindowLongW(d->hwnd, GWL_STYLE), FALSE,
                       (DWORD)GetWindowLongW(d->hwnd, GWL_EXSTYLE));
    SetWindowPos(d->hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (isVisible) {
        SendMessageW(d->hwnd, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(d->hwnd, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
}

// "1-3, 5" in the Pages edit; spaces allowed
static bool ParsePagesEdit(PrintDlgState* d, Vec<PRINTPAGERANGE>& ranges) {
    TempStr s = str::DupTemp(HwndGetTextTemp(Item(d, IDC_PR_PAGES_EDIT)));
    // the chars move down in place; it returns how many it removed
    s.len -= str::RemoveCharsInPlace(s, StrL(" "));
    Vec<PageRange> parsed;
    if (!ParsePageRanges(s, parsed)) {
        logf("PrintDialog: invalid page range '%s'\n", s);
        return false;
    }
    for (PageRange& r : parsed) {
        int end = std::min(r.end, d->nPages);
        if (r.start > d->nPages) {
            continue;
        }
        VecAppend(ranges, PRINTPAGERANGE{(DWORD)r.start, (DWORD)end});
    }
    return len(ranges) > 0;
}

// the visible part of the current page, in page coordinates
static RectF CurrentViewRect(PrintDlgState* d) {
    PageInfo* pi = d->dm->GetPageInfo(d->curPageNo);
    if (!pi) {
        return d->engine->PageMediabox(d->curPageNo);
    }
    Size viewport = d->dm->GetViewPort().Size();
    Rect visible = pi->pageOnScreen.Intersect(Rect{0, 0, viewport.dx, viewport.dy});
    if (visible.IsEmpty()) {
        return d->engine->PageMediabox(d->curPageNo);
    }
    return d->dm->CvtFromScreen(visible, d->curPageNo);
}

static bool GetRanges(PrintDlgState* d, Vec<PRINTPAGERANGE>& ranges) {
    switch (d->pagesMode) {
        case PagesToPrint::All:
            VecAppend(ranges, PRINTPAGERANGE{1, (DWORD)d->nPages});
            return true;
        case PagesToPrint::Current:
        case PagesToPrint::CurrentView:
            VecAppend(ranges, PRINTPAGERANGE{(DWORD)d->curPageNo, (DWORD)d->curPageNo});
            return true;
        case PagesToPrint::Pages:
            return ParsePagesEdit(d, ranges);
    }
    return false;
}

static void ReadControls(PrintDlgState* d) {
    PrintJobOptions& o = d->opts;
    o.copies = limitValue(GetItemInt(d, IDC_PR_COPIES, 1), 1, 999);
    o.collate = IsChecked(d, IDC_PR_COLLATE);
    o.grayscale = IsChecked(d, IDC_PR_GRAYSCALE);
    o.saveInk = IsChecked(d, IDC_PR_SAVE_INK);

    d->pagesMode = PagesToPrint::All;
    if (IsChecked(d, IDC_PR_CURRENT)) {
        d->pagesMode = PagesToPrint::Current;
    } else if (IsChecked(d, IDC_PR_PAGES)) {
        d->pagesMode = PagesToPrint::Pages;
    } else if (IsChecked(d, IDC_PR_CURRENT_VIEW)) {
        d->pagesMode = PagesToPrint::CurrentView;
    }
    o.reverse = IsChecked(d, IDC_PR_REVERSE);
    PrintRangeAdv oddEven[] = {PrintRangeAdv::All, PrintRangeAdv::Odd, PrintRangeAdv::Even};
    d->adv.range = oddEven[std::min(ComboIdx(d, IDC_PR_ODD_EVEN), 2)];
    o.viewPageNo = 0;
    if (d->pagesMode == PagesToPrint::CurrentView) {
        o.viewPageNo = d->curPageNo;
        o.viewRect = CurrentViewRect(d);
    }

    o.sizing = CheckedSizing(d);
    o.sizeMode = PrintSizeMode::Shrink;
    if (IsChecked(d, IDC_PR_FIT)) {
        o.sizeMode = PrintSizeMode::Fit;
    } else if (IsChecked(d, IDC_PR_ACTUAL)) {
        o.sizeMode = PrintSizeMode::Actual;
    } else if (IsChecked(d, IDC_PR_CUSTOM)) {
        o.sizeMode = PrintSizeMode::Custom;
    }
    o.customScale = limitValue(GetItemFloat(d, IDC_PR_CUSTOM_EDIT, 100), 1.f, 1000.f);
    o.rotation = std::min(ComboIdx(d, IDC_PR_ROTATE), 3) * 90;
    o.position = (PrintPosition)std::min(ComboIdx(d, IDC_PR_POSITION), 8);
    o.offsetXMm = limitValue(GetItemLength(d, IDC_PR_OFFSET_X, 0), -1000.f, 1000.f);
    o.offsetYMm = limitValue(GetItemLength(d, IDC_PR_OFFSET_Y, 0), -1000.f, 1000.f);
    o.marginLeftMm = limitValue(GetItemLength(d, IDC_PR_MARGIN_LEFT, 0), 0.f, 200.f);
    o.marginRightMm = limitValue(GetItemLength(d, IDC_PR_MARGIN_RIGHT, 0), 0.f, 200.f);
    o.marginTopMm = limitValue(GetItemLength(d, IDC_PR_MARGIN_TOP, 0), 0.f, 200.f);
    o.marginBottomMm = limitValue(GetItemLength(d, IDC_PR_MARGIN_BOTTOM, 0), 0.f, 200.f);

    o.posterTileScale = limitValue(GetItemFloat(d, IDC_PR_TILE_EDIT, 100), 1.f, 10000.f);
    o.posterOverlapMm = limitValue(GetItemLength(d, IDC_PR_OVERLAP_EDIT, 0), 0.f, 127.f);
    o.posterCutMarks = IsChecked(d, IDC_PR_CUT_MARKS);
    o.posterLabels = IsChecked(d, IDC_PR_LABELS);
    o.posterLargeOnly = IsChecked(d, IDC_PR_LARGE_ONLY);

    int perSheet = ComboIdx(d, IDC_PR_PER_SHEET);
    if (perSheet < kPerSheetCustomIdx) {
        o.multiCols = kPerSheetPresets[perSheet].cols;
        o.multiRows = kPerSheetPresets[perSheet].rows;
    } else {
        o.multiCols = limitValue(GetItemInt(d, IDC_PR_COLS_EDIT, 2), 1, kMaxGridSide);
        o.multiRows = limitValue(GetItemInt(d, IDC_PR_ROWS_EDIT, 2), 1, kMaxGridSide);
    }
    o.multiOrder = (MultiPageOrder)std::min(ComboIdx(d, IDC_PR_ORDER), 3);
    o.multiBorder = IsChecked(d, IDC_PR_BORDER);
    o.multiAutoRotate = IsChecked(d, IDC_PR_AUTO_ROTATE);

    o.bookletSubset = (BookletSubset)std::min(ComboIdx(d, IDC_PR_SUBSET), 2);
    o.bookletFirstSheet = std::max(1, GetItemInt(d, IDC_PR_SHEETS_FROM, 1));
    o.bookletLastSheet = std::max(0, GetItemInt(d, IDC_PR_SHEETS_TO, 0));
    o.bookletBinding = (BookletBinding)std::min(ComboIdx(d, IDC_PR_BINDING), 3);

    o.duplex = PrintDuplex::None;
    bool canDuplex = d->printer && d->printer->isDuplex && ShowsDuplex(o.sizing);
    if (canDuplex && IsChecked(d, IDC_PR_DUPLEX)) {
        o.duplex = IsChecked(d, IDC_PR_FLIP_SHORT) ? PrintDuplex::ShortEdge : PrintDuplex::LongEdge;
    }
    o.orientation = PrintOrientation::Auto;
    if (IsChecked(d, IDC_PR_ORIENT_PORTRAIT)) {
        o.orientation = PrintOrientation::Portrait;
    } else if (IsChecked(d, IDC_PR_ORIENT_LANDSCAPE)) {
        o.orientation = PrintOrientation::Landscape;
    }

    o.annots = kCommentsModes[std::min(ComboIdx(d, IDC_PR_COMMENTS), 3)];
}

static void UpdateEnabledStates(PrintDlgState* d) {
    // a single page document has nothing to pick from
    bool manyPages = d->nPages > 1;
    int pageCtrls[] = {IDC_PR_CURRENT, IDC_PR_PAGES, IDC_PR_REVERSE, IDC_PR_ODD_EVEN_LABEL, IDC_PR_ODD_EVEN};
    for (int id : pageCtrls) {
        EnableItem(d, id, manyPages);
    }
    EnableItem(d, IDC_PR_PAGES_EDIT, manyPages && d->pagesMode == PagesToPrint::Pages);
    EnableItem(d, IDC_PR_CUSTOM_EDIT, d->opts.sizeMode == PrintSizeMode::Custom);
    bool customGrid = ComboIdx(d, IDC_PR_PER_SHEET) == kPerSheetCustomIdx;
    EnableItem(d, IDC_PR_COLS_EDIT, customGrid);
    EnableItem(d, IDC_PR_ROWS_EDIT, customGrid);

    bool canDuplex = d->printer && d->printer->isDuplex;
    EnableItem(d, IDC_PR_DUPLEX, canDuplex);
    bool flip = canDuplex && IsChecked(d, IDC_PR_DUPLEX);
    EnableItem(d, IDC_PR_FLIP_LONG, flip);
    EnableItem(d, IDC_PR_FLIP_SHORT, flip);
}

static float PrintFileDpi(PrintDlgState* d) {
    float dpi = d->engine->fileDPI;
    if (IsEngineImages(d->engine) && d->adv.dpiOverride > 0) {
        dpi = d->adv.dpiOverride;
    }
    return dpi > 0 ? dpi : 96.f;
}

// a label repaints on every WM_SETTEXT, so only when its text changes
static void SetTextIfChanged(HWND hwnd, Str text) {
    if (!str::Eq(HwndGetTextTemp(hwnd), text)) {
        HwndSetText(hwnd, text);
    }
}

// what one step of the preview shows: a sheet, or for Poster the tiles of a page
static int PreviewCount(PrintDlgState* d) {
    int n = len(d->plan.sheets);
    if (d->opts.sizing != PrintSizing::Poster) {
        return n;
    }
    int count = 0;
    int prevPageNo = -1;
    for (int i = 0; i < n; i++) {
        int pageNo = d->plan.sheets[i].posterPageNo;
        if (pageNo == 0 || pageNo != prevPageNo) {
            count++;
        }
        prevPageNo = pageNo;
    }
    return count;
}

static void PreviewSheets(PrintDlgState* d, int idx, int& first, int& count) {
    first = 0;
    count = 0;
    int n = len(d->plan.sheets);
    if (d->opts.sizing != PrintSizing::Poster) {
        if (idx < n) {
            first = idx;
            count = 1;
        }
        return;
    }
    int group = -1;
    int prevPageNo = -1;
    for (int i = 0; i < n; i++) {
        int pageNo = d->plan.sheets[i].posterPageNo;
        if (pageNo == 0 || pageNo != prevPageNo) {
            group++;
        }
        prevPageNo = pageNo;
        if (group == idx) {
            if (count == 0) {
                first = i;
            }
            count++;
        }
    }
}

// the size of what the preview shows, in device pixels: the paper, or the poster
static Size PreviewExtent(PrintDlgState* d, int first, int count) {
    if (d->opts.sizing != PrintSizing::Poster || count == 0) {
        return d->geo.paper;
    }
    Size extent;
    for (int i = first; i < first + count; i++) {
        const PrintSheet& sheet = d->plan.sheets[i];
        extent.dx = std::max(extent.dx, sheet.tileOrigin.x + sheet.tileSize.dx);
        extent.dy = std::max(extent.dy, sheet.tileOrigin.y + sheet.tileSize.dy);
    }
    return extent;
}

static void UpdatePreviewLabels(PrintDlgState* d) {
    int n = PreviewCount(d);
    d->previewIdx = limitValue(d->previewIdx, 0, std::max(0, n - 1));
    HWND slider = Item(d, IDC_PR_SLIDER);
    SendMessageW(slider, TBM_SETRANGEMIN, FALSE, 0);
    SendMessageW(slider, TBM_SETRANGEMAX, TRUE, std::max(0, n - 1));
    SendMessageW(slider, TBM_SETPOS, TRUE, d->previewIdx);
    EnableItem(d, IDC_PR_SLIDER, n > 1);
    EnableItem(d, IDC_PR_PREV, d->previewIdx > 0);
    EnableItem(d, IDC_PR_NEXT, d->previewIdx < n - 1);

    int first, count;
    PreviewSheets(d, d->previewIdx, first, count);
    int no = n > 0 ? d->previewIdx + 1 : 0;
    TempStr sheetText = fmt(Tr("Page %d of %d").s, no, n);
    PrintSizing sizing = d->opts.sizing;
    // several pages on a sheet: also the first page's number
    if ((sizing == PrintSizing::Multiple || sizing == PrintSizing::Booklet) && count > 0) {
        const PrintSheet& sheet = d->plan.sheets[first];
        int pageNo = sheet.nCells > 0 ? d->plan.cells[sheet.firstCell].pageNo : 0;
        sheetText = fmt("%s (%d)", sheetText, pageNo);
    }
    SetTextIfChanged(Item(d, IDC_PR_SHEET_LABEL), sheetText);

    TempStr scaleText = str::DupTemp(StrL(""));
    float dpiFactor = std::min(d->geo.dpiX, d->geo.dpiY) / PrintFileDpi(d);
    if (count > 0 && d->plan.sheets[first].nCells > 0 &&
        (sizing == PrintSizing::Size || sizing == PrintSizing::Poster)) {
        const PrintCell& cell = d->plan.cells[d->plan.sheets[first].firstCell];
        int scale = (int)lroundf(cell.zoom / dpiFactor * 100.f);
        scaleText = fmt("%s %d%%", Tr("Scale:"), scale);
        if (sizing == PrintSizing::Poster) {
            scaleText = fmt("%s %s %d", scaleText, Tr("Pages:"), count);
        }
    }
    SetTextIfChanged(Item(d, IDC_PR_SCALE_LABEL), scaleText);

    if (d->previewBmp) {
        DeleteObject(d->previewBmp);
        d->previewBmp = nullptr;
    }
    InvalidateRect(Item(d, IDC_PR_PREVIEW), nullptr, FALSE);
}

// re-reads the choices, lays the dialog out and rebuilds the preview
static void UpdatePreview(PrintDlgState* d) {
    if (d->updating) {
        return;
    }
    ReadControls(d);
    UpdateEnabledStates(d);
    LayoutDialog(d);

    d->geo = PrintGeometry{};
    d->geo.paper = Size(850, 1100);
    d->geo.printable = Rect(0, 0, 850, 1100);
    d->geo.dpiX = d->geo.dpiY = 100;
    if (d->printer && d->devMode) {
        DEVMODEW* dm = DupDevMode(d->devMode);
        ApplyPrintJobToDevMode(dm, d->opts);
        HDC hdc = CreateICW(nullptr, CWStrTemp(d->printer->name), nullptr, dm);
        if (hdc) {
            GetPrintGeometry(hdc, d->geo);
            DeleteDC(hdc);
        }
        free(dm);
    }

    Vec<PRINTPAGERANGE> ranges;
    Vec<int> pages;
    if (GetRanges(d, ranges)) {
        pages = PrintPageList(ranges, d->nPages, d->adv.range, d->opts.reverse);
    }
    BuildPrintPlan(*d->engine, pages, d->adv, d->opts, d->geo, d->plan);
    UpdatePreviewLabels(d);
}

static void DrawDashedLine(HDC hdc, int x1, int y1, int x2, int y2) {
    HPEN pen = CreatePen(PS_DASH, 1, RGB(64, 64, 64));
    HGDIOBJ prev = SelectObject(hdc, pen);
    MoveToEx(hdc, x1, y1, nullptr);
    LineTo(hdc, x2, y2);
    SelectObject(hdc, prev);
    DeleteObject(pen);
}

/*
 * The paper (or the whole poster) with its size in inches above it, scaled to
 * fit the preview, and what prints on it.
 */
static void RenderPreview(PrintDlgState* d, HDC hdc, Rect rc) {
    RECT all{0, 0, rc.dx, rc.dy};
    HBRUSH bg = CreateSolidBrush(ThemeWindowBackgroundColor());
    FillRect(hdc, &all, bg);
    DeleteObject(bg);

    int first, count;
    PreviewSheets(d, d->previewIdx, first, count);
    Size extent = PreviewExtent(d, first, count);
    if (extent.dx <= 0 || extent.dy <= 0) {
        return;
    }

    HFONT font = (HFONT)SendMessageW(d->hwnd, WM_GETFONT, 0, 0);
    HGDIOBJ prevFont = SelectObject(hdc, font);
    TEXTMETRICW tm{};
    GetTextMetricsW(hdc, &tm);
    int labelDy = tm.tmHeight + 6;

    constexpr int kShadow = 3;
    int availDx = rc.dx - kShadow - 2;
    int availDy = rc.dy - kShadow - 2 - labelDy;
    float scale = std::min((float)availDx / extent.dx, (float)availDy / extent.dy);
    int paperDx = (int)(extent.dx * scale);
    int paperDy = (int)(extent.dy * scale);
    int x = (rc.dx - paperDx) / 2;
    int y = labelDy + (availDy - paperDy) / 2;

    // "210 x 297 mm" or "8.27 x 11.69 in" right above the paper
    float inchDx = extent.dx / d->geo.dpiX;
    float inchDy = extent.dy / d->geo.dpiY;
    TempStr label = d->inches ? fmt("%.2f x %.2f %s", inchDx, inchDy, LengthUnit(d))
                              : fmt("%.0f x %.0f %s", inchDx * 25.4f, inchDy * 25.4f, LengthUnit(d));
    RECT labelRc{0, y - labelDy, rc.dx, y - 4};
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, ThemeWindowTextColor());
    DrawTextW(hdc, CWStrTemp(label), -1, &labelRc, DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, prevFont);

    RECT shadow{x + kShadow, y + kShadow, x + paperDx + kShadow, y + paperDy + kShadow};
    FillRect(hdc, &shadow, (HBRUSH)GetStockObject(GRAY_BRUSH));
    RECT paper{x, y, x + paperDx, y + paperDy};
    FillRect(hdc, &paper, (HBRUSH)GetStockObject(WHITE_BRUSH));

    // drawing outside the paper (Actual size of a large page) is cut off
    HRGN clip = CreateRectRgn(x, y, x + paperDx, y + paperDy);
    SelectClipRgn(hdc, clip);
    if (d->opts.sizing != PrintSizing::Poster) {
        Point origin(x + (int)(d->geo.printable.x * scale), y + (int)(d->geo.printable.y * scale));
        DrawPrintSheetPreview(*d->engine, hdc, d->plan, first, d->opts, d->geo, scale, origin);
        // the margins, dotted
        const PrintJobOptions& o = d->opts;
        if (o.marginLeftMm > 0 || o.marginTopMm > 0 || o.marginRightMm > 0 || o.marginBottomMm > 0) {
            Rect area = PrintContentArea(o, d->geo);
            int x0 = x + (int)(area.x * scale);
            int y0 = y + (int)(area.y * scale);
            int x1 = x + (int)((area.x + area.dx) * scale);
            int y1 = y + (int)((area.y + area.dy) * scale);
            HPEN pen = CreatePen(PS_DOT, 1, RGB(128, 128, 128));
            HGDIOBJ prevPen = SelectObject(hdc, pen);
            HGDIOBJ prevBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, x0, y0, x1, y1);
            SelectObject(hdc, prevBrush);
            SelectObject(hdc, prevPen);
            DeleteObject(pen);
        }
    } else {
        // the tiles put together, with dashed lines where they meet
        for (int i = first; i < first + count; i++) {
            const PrintSheet& sheet = d->plan.sheets[i];
            // a tile is drawn at its place on the paper: move it back to its place on the poster
            Point tilePos = sheet.nCells > 0 ? d->plan.cells[sheet.firstCell].pos : Point();
            Point origin(x + (int)((sheet.tileOrigin.x - tilePos.x) * scale),
                         y + (int)((sheet.tileOrigin.y - tilePos.y) * scale));
            DrawPrintSheetPreview(*d->engine, hdc, d->plan, i, d->opts, d->geo, scale, origin);
        }
        for (int i = first; i < first + count; i++) {
            const PrintSheet& sheet = d->plan.sheets[i];
            int tx = x + (int)(sheet.tileOrigin.x * scale);
            int ty = y + (int)(sheet.tileOrigin.y * scale);
            if (sheet.tileOrigin.x > 0) {
                DrawDashedLine(hdc, tx, ty, tx, ty + (int)(sheet.tileSize.dy * scale));
            }
            if (sheet.tileOrigin.y > 0) {
                DrawDashedLine(hdc, tx, ty, tx + (int)(sheet.tileSize.dx * scale), ty);
            }
        }
    }
    SelectClipRgn(hdc, nullptr);
    DeleteObject(clip);
    FrameRect(hdc, &paper, (HBRUSH)GetStockObject(BLACK_BRUSH));
}

static void DrawPreview(PrintDlgState* d, DRAWITEMSTRUCT* dis) {
    Rect rc = ToRect(dis->rcItem);
    if (!d->previewBmp) {
        d->previewBmp = CreateCompatibleBitmap(dis->hDC, rc.dx, rc.dy);
        HDC memDC = CreateCompatibleDC(dis->hDC);
        HGDIOBJ prevBmp = SelectObject(memDC, d->previewBmp);
        RenderPreview(d, memDC, rc);
        SelectObject(memDC, prevBmp);
        DeleteDC(memDC);
    }
    HDC memDC = CreateCompatibleDC(dis->hDC);
    HGDIOBJ prevBmp = SelectObject(memDC, d->previewBmp);
    BitBlt(dis->hDC, rc.x, rc.y, rc.dx, rc.dy, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, prevBmp);
    DeleteDC(memDC);
}

// the round (?) icon next to Help, anti-aliased
static void DrawRoundIcon(DRAWITEMSTRUCT* dis, const WCHAR* glyph) {
    Rect rc = ToRect(dis->rcItem);
    HBRUSH bg = CreateSolidBrush(ThemeWindowBackgroundColor());
    FillRect(dis->hDC, &dis->rcItem, bg);
    DeleteObject(bg);

    Color col = ThemeWindowDarkerTextColor();
    Gdiplus::Color gdipCol(255, GetRValue(col), GetGValue(col), GetBValue(col));
    Gdiplus::Graphics gfx(dis->hDC);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    float size = (float)std::min(rc.dx, rc.dy);
    float penDx = std::max(1.f, size / 14.f);
    float x = (float)rc.x + ((float)rc.dx - size) / 2.f;
    float y = (float)rc.y + ((float)rc.dy - size) / 2.f;
    Gdiplus::Pen pen(gdipCol, penDx);
    float inset = penDx / 2.f + 0.5f;
    gfx.DrawEllipse(&pen, x + inset, y + inset, size - 2 * inset, size - 2 * inset);

    Gdiplus::Font font(L"Segoe UI", size * 0.62f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::SolidBrush brush(gdipCol);
    Gdiplus::RectF box(x, y + size * 0.03f, size, size);
    gfx.DrawString(glyph, -1, &font, box, &format, &brush);
}

// Paper: the paper sizes, sources (trays) and print qualities of the printer,
// the current ones selected
static void SelectPaperOptions(PrintDlgState* d) {
    Printer* p = d->printer;
    DEVMODEW* dm = d->devMode;
    if (!p || !dm) {
        return;
    }
    int paperIdx = -1;
    for (int i = 0; i < p->nPaperSizes; i++) {
        if (p->papers[i] == dm->dmPaperSize) {
            paperIdx = i;
        }
    }
    CbSetCurrentSelection(Item(d, IDC_PR_PAPER_SIZE), paperIdx);
    int sourceIdx = -1;
    for (int i = 0; i < p->nBins; i++) {
        if (p->bins[i] == dm->dmDefaultSource) {
            sourceIdx = i;
        }
    }
    CbSetCurrentSelection(Item(d, IDC_PR_SOURCE), sourceIdx);
    // 0 is "Printer default"
    int qualityIdx = 0;
    bool hasQuality = (dm->dmFields & DM_PRINTQUALITY) && dm->dmPrintQuality > 0;
    for (int i = 0; hasQuality && i < len(d->resolutions); i++) {
        Point r = d->resolutions[i];
        bool sameY = !(dm->dmFields & DM_YRESOLUTION) || dm->dmYResolution == r.y;
        if (r.x == dm->dmPrintQuality && sameY) {
            qualityIdx = i + 1;
        }
    }
    CbSetCurrentSelection(Item(d, IDC_PR_QUALITY), qualityIdx);
}

static void FillPaperOptions(PrintDlgState* d) {
    HWND paper = Item(d, IDC_PR_PAPER_SIZE);
    HWND source = Item(d, IDC_PR_SOURCE);
    HWND quality = Item(d, IDC_PR_QUALITY);
    SendMessageW(paper, CB_RESETCONTENT, 0, 0);
    SendMessageW(source, CB_RESETCONTENT, 0, 0);
    SendMessageW(quality, CB_RESETCONTENT, 0, 0);
    VecReset(d->resolutions);
    Printer* p = d->printer;
    int nPapers = p ? p->nPaperSizes : 0;
    int nBins = p ? p->nBins : 0;
    for (int i = 0; i < nPapers; i++) {
        CbAddString(paper, p->paperNames.At(i));
    }
    for (int i = 0; i < nBins; i++) {
        CbAddString(source, p->binNames.At(i));
    }

    // the resolutions the driver offers: pairs of x, y dpi
    CbAddString(quality, Tr("Printer default"));
    WCHAR* nameW = p ? CWStrTemp(p->name) : nullptr;
    int n = nameW ? (int)DeviceCapabilitiesW(nameW, nullptr, DC_ENUMRESOLUTIONS, nullptr, nullptr) : 0;
    if (n > 0) {
        LONG* res = AllocArray<LONG>(n * 2);
        DeviceCapabilitiesW(nameW, nullptr, DC_ENUMRESOLUTIONS, (WCHAR*)res, nullptr);
        for (int i = 0; i < n; i++) {
            Point r(res[i * 2], res[i * 2 + 1]);
            if (r.x <= 0 || r.y <= 0) {
                continue;
            }
            VecAppend(d->resolutions, r);
            TempStr text = r.x == r.y ? fmt("%d dpi", r.x) : fmt("%d x %d dpi", r.x, r.y);
            CbAddString(quality, text);
        }
        free(res);
    }
    EnableItem(d, IDC_PR_PAPER_SIZE, nPapers > 0);
    EnableItem(d, IDC_PR_SOURCE, nBins > 0);
    EnableItem(d, IDC_PR_QUALITY, len(d->resolutions) > 0);
    SelectPaperOptions(d);
}

// a paper size, source or quality picked in the dialog goes into the printer settings
static void OnPaperOptionChanged(PrintDlgState* d, int id) {
    Printer* p = d->printer;
    DEVMODEW* dm = d->devMode;
    int idx = CbGetCurrentSelection(Item(d, id));
    if (!p || !dm || idx < 0) {
        return;
    }
    if (id == IDC_PR_PAPER_SIZE && idx < p->nPaperSizes) {
        dm->dmPaperSize = (short)p->papers[idx];
        dm->dmFields |= DM_PAPERSIZE;
        // a standard size replaces a custom one
        dm->dmFields &= ~(DM_PAPERLENGTH | DM_PAPERWIDTH);
    } else if (id == IDC_PR_SOURCE && idx < p->nBins) {
        dm->dmDefaultSource = (short)p->bins[idx];
        dm->dmFields |= DM_DEFAULTSOURCE;
    } else if (id == IDC_PR_QUALITY && idx == 0) {
        // back to what the driver uses by default
        DEVMODEW* def = p->devMode;
        dm->dmPrintQuality = def->dmPrintQuality;
        dm->dmYResolution = def->dmYResolution;
        dm->dmFields &= ~(DM_PRINTQUALITY | DM_YRESOLUTION);
        dm->dmFields |= def->dmFields & (DM_PRINTQUALITY | DM_YRESOLUTION);
    } else if (id == IDC_PR_QUALITY && idx <= len(d->resolutions)) {
        Point r = d->resolutions[idx - 1];
        dm->dmPrintQuality = (short)r.x;
        dm->dmYResolution = (short)r.y;
        dm->dmFields |= DM_PRINTQUALITY | DM_YRESOLUTION;
    }
}

static void LoadPrinter(PrintDlgState* d, Str name) {
    delete d->printer;
    free(d->devMode);
    d->devMode = nullptr;
    d->printer = NewPrinter(name);
    if (!d->printer) {
        logf("PrintDialog: NewPrinter('%s') failed\n", name);
        return;
    }
    bool isLast = gLastDevMode && str::Eq(name, gLastPrinterName);
    d->devMode = DupDevMode(isLast ? gLastDevMode : d->printer->devMode);
    FillPaperOptions(d);
}

static void FillPrinters(PrintDlgState* d) {
    DWORD needed = 0;
    DWORD count = 0;
    DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
    EnumPrintersW(flags, nullptr, 4, nullptr, 0, &needed, &count);
    BYTE* buf = AllocArray<BYTE>((int)needed + 1);
    if (buf && EnumPrintersW(flags, nullptr, 4, buf, needed, &needed, &count)) {
        auto* infos = (PRINTER_INFO_4W*)buf;
        for (DWORD i = 0; i < count; i++) {
            d->printerNames.Append(ToUtf8Temp(infos[i].pPrinterName));
        }
    }
    free(buf);

    Str wanted = gLastPrinterName;
    if (len(wanted) == 0) {
        wanted = GetDefaultPrinterNameTemp();
    }
    HWND cb = Item(d, IDC_PR_PRINTER);
    int sel = 0;
    for (int i = 0; i < len(d->printerNames); i++) {
        Str name = d->printerNames.At(i);
        CbAddString(cb, name);
        if (str::Eq(name, wanted)) {
            sel = i;
        }
    }
    if (len(d->printerNames) > 0) {
        CbSetCurrentSelection(cb, sel);
        LoadPrinter(d, d->printerNames.At(sel));
    }
}

static void SetOrientation(PrintDlgState* d, PrintOrientation orient) {
    SetChecked(d, IDC_PR_ORIENT_AUTO, orient == PrintOrientation::Auto);
    SetChecked(d, IDC_PR_ORIENT_PORTRAIT, orient == PrintOrientation::Portrait);
    SetChecked(d, IDC_PR_ORIENT_LANDSCAPE, orient == PrintOrientation::Landscape);
}

// Properties / Page Setup changed the driver settings: show them in the dialog
static void SyncFromDevMode(PrintDlgState* d) {
    DEVMODEW* dm = d->devMode;
    if (!dm) {
        return;
    }
    if (dm->dmFields & DM_COPIES) {
        SetDlgItemInt(d->hwnd, IDC_PR_COPIES, std::max(1, (int)dm->dmCopies), FALSE);
    }
    if (dm->dmFields & DM_COLOR) {
        SetChecked(d, IDC_PR_GRAYSCALE, dm->dmColor == DMCOLOR_MONOCHROME);
    }
    if (dm->dmFields & DM_DUPLEX) {
        SetChecked(d, IDC_PR_DUPLEX, dm->dmDuplex != DMDUP_SIMPLEX);
        bool shortEdge = dm->dmDuplex == DMDUP_HORIZONTAL;
        SetChecked(d, IDC_PR_FLIP_LONG, !shortEdge);
        SetChecked(d, IDC_PR_FLIP_SHORT, shortEdge);
    }
    if (dm->dmFields & DM_ORIENTATION) {
        bool portrait = dm->dmOrientation == DMORIENT_PORTRAIT;
        SetOrientation(d, portrait ? PrintOrientation::Portrait : PrintOrientation::Landscape);
    }
    SelectPaperOptions(d);
}

// the printer driver's own settings dialog
static void OnPrinterProperties(PrintDlgState* d) {
    if (!d->printer || !d->devMode) {
        return;
    }
    WCHAR* nameW = CWStrTemp(d->printer->name);
    HANDLE hPrinter = nullptr;
    if (!OpenPrinterW(nameW, &hPrinter, nullptr)) {
        return;
    }
    // the job's own settings go in first so the driver shows them
    ReadControls(d);
    ApplyPrintJobToDevMode(d->devMode, d->opts);
    DWORD mode = DM_IN_BUFFER | DM_OUT_BUFFER | DM_IN_PROMPT;
    LONG res = DocumentPropertiesW(d->hwnd, hPrinter, nameW, d->devMode, d->devMode, mode);
    ClosePrinter(hPrinter);
    if (res == IDOK) {
        SyncFromDevMode(d);
    }
}

// Print to File: where the printer's output goes
static bool AskOutputFile(PrintDlgState* d) {
    WCHAR path[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = d->hwnd;
    ofn.lpstrFilter = L"Printer files (*.prn)\0*.prn\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = dimof(path);
    ofn.lpstrDefExt = L"prn";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) {
        return false;
    }
    str::ReplaceWithCopy(&d->outputFile, ToUtf8Temp(path));
    return true;
}

static HFONT CreateDerivedFont(HWND hwnd, int weight, const WCHAR* face) {
    LOGFONTW lf{};
    GetObjectW((HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0), sizeof(lf), &lf);
    lf.lfWeight = weight;
    if (face) {
        lf.lfCharSet = SYMBOL_CHARSET;
        wcscpy_s(lf.lfFaceName, face);
    }
    return CreateFontIndirectW(&lf);
}

static void AddTooltip(PrintDlgState* d, int id, Str text) {
    HWND tip = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT,
                               CW_USEDEFAULT, CW_USEDEFAULT, d->hwnd, nullptr, nullptr, nullptr);
    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = d->hwnd;
    ti.uId = (UINT_PTR)Item(d, id);
    ti.lpszText = CWStrTemp(text);
    SendMessageW(tip, TTM_SETMAXTIPWIDTH, 0, 300);
    SendMessageW(tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

static void InitControls(PrintDlgState* d) {
    HWND hwnd = d->hwnd;
    d->updating = true;

    d->boldFont = CreateDerivedFont(hwnd, FW_BOLD, nullptr);
    d->arrowFont = CreateDerivedFont(hwnd, FW_NORMAL, L"Marlett");
    int titles[] = {IDC_PR_PAGES_TITLE, IDC_PR_SIZING_TITLE, IDC_PR_COMMENTS_TITLE, IDC_PR_PAPER_TITLE};
    for (int id : titles) {
        HwndSetFont(Item(d, id), d->boldFont);
    }
    HwndSetFont(Item(d, IDC_PR_MORE_ARROW), d->arrowFont);
    HwndSetText(Item(d, IDC_PR_HELP), fmt("<a>%s</a>", Tr("Help")));
    AddTooltip(d, IDC_PR_HELP_ICON, Tr("Help"));

    FillPrinters(d);

    const PrintJobOptions& o = d->opts;
    SendMessageW(Item(d, IDC_PR_COPIES_SPIN), UDM_SETRANGE32, 1, 999);
    SetDlgItemInt(hwnd, IDC_PR_COPIES, 1, FALSE);
    SetChecked(d, IDC_PR_COLLATE, o.collate);
    SetChecked(d, IDC_PR_GRAYSCALE, o.grayscale);
    SetChecked(d, IDC_PR_SAVE_INK, o.saveInk);

    SetChecked(d, IDC_PR_ALL, true);
    TempStr allPages = d->nPages > 1 ? fmt("1-%d", d->nPages) : fmt("%d", d->nPages);
    HwndSetText(Item(d, IDC_PR_PAGES_EDIT), allPages);
    SetChecked(d, IDC_PR_REVERSE, o.reverse);
    Str oddEven[] = {Tr("All pages in range"), Tr("Odd pages only"), Tr("Even pages only")};
    FillCombo(d, IDC_PR_ODD_EVEN, oddEven, dimof(oddEven), 0);

    int sizingIds[] = {IDC_PR_SIZE, IDC_PR_POSTER, IDC_PR_MULTIPLE, IDC_PR_BOOKLET};
    SetChecked(d, sizingIds[(int)o.sizing], true);
    int sizeModeIds[] = {IDC_PR_FIT, IDC_PR_ACTUAL, IDC_PR_SHRINK, IDC_PR_CUSTOM};
    SetChecked(d, sizeModeIds[(int)o.sizeMode], true);
    SetItemFloat(d, IDC_PR_CUSTOM_EDIT, o.customScale);
    Str rotations[] = {Tr("None"), Tr("90 degrees"), Tr("180 degrees"), Tr("270 degrees")};
    FillCombo(d, IDC_PR_ROTATE, rotations, dimof(rotations), (o.rotation / 90) % 4);
    Str positions[] = {Tr("Center"), Tr("Top left"),    Tr("Top"),    Tr("Top right"),   Tr("Left"),
                       Tr("Right"),  Tr("Bottom left"), Tr("Bottom"), Tr("Bottom right")};
    FillCombo(d, IDC_PR_POSITION, positions, dimof(positions), (int)o.position);
    SetItemLength(d, IDC_PR_OFFSET_X, o.offsetXMm);
    SetItemLength(d, IDC_PR_OFFSET_Y, o.offsetYMm);
    SetItemLength(d, IDC_PR_MARGIN_LEFT, o.marginLeftMm);
    SetItemLength(d, IDC_PR_MARGIN_RIGHT, o.marginRightMm);
    SetItemLength(d, IDC_PR_MARGIN_TOP, o.marginTopMm);
    SetItemLength(d, IDC_PR_MARGIN_BOTTOM, o.marginBottomMm);
    int unitLabels[] = {IDC_PR_OFFSET_X_UNIT, IDC_PR_OFFSET_UNIT, IDC_PR_OVERLAP_UNIT};
    for (int id : unitLabels) {
        HwndSetText(Item(d, id), LengthUnit(d));
    }
    HwndSetText(Item(d, IDC_PR_MARGINS_LABEL), fmt("%s (%s):", Tr("Margins"), LengthUnit(d)));

    SetItemFloat(d, IDC_PR_TILE_EDIT, o.posterTileScale);
    SetItemLength(d, IDC_PR_OVERLAP_EDIT, o.posterOverlapMm);
    SetChecked(d, IDC_PR_CUT_MARKS, o.posterCutMarks);
    SetChecked(d, IDC_PR_LABELS, o.posterLabels);
    SetChecked(d, IDC_PR_LARGE_ONLY, o.posterLargeOnly);

    Str perSheet[] = {StrL("2"), StrL("4"), StrL("6"), StrL("9"), StrL("16"), Tr("Custom...")};
    int perSheetIdx = kPerSheetCustomIdx;
    for (int i = 0; i < kPerSheetCustomIdx; i++) {
        if (kPerSheetPresets[i].cols == o.multiCols && kPerSheetPresets[i].rows == o.multiRows) {
            perSheetIdx = i;
        }
    }
    FillCombo(d, IDC_PR_PER_SHEET, perSheet, dimof(perSheet), perSheetIdx);
    SetDlgItemInt(hwnd, IDC_PR_COLS_EDIT, o.multiCols, FALSE);
    SetDlgItemInt(hwnd, IDC_PR_ROWS_EDIT, o.multiRows, FALSE);
    Str order[] = {Tr("Horizontal"), Tr("Horizontal Reversed"), Tr("Vertical"), Tr("Vertical Reversed")};
    FillCombo(d, IDC_PR_ORDER, order, dimof(order), (int)o.multiOrder);
    SetChecked(d, IDC_PR_BORDER, o.multiBorder);
    SetChecked(d, IDC_PR_AUTO_ROTATE, o.multiAutoRotate);

    Str subset[] = {Tr("Both sides"), Tr("Front side only"), Tr("Back side only")};
    FillCombo(d, IDC_PR_SUBSET, subset, dimof(subset), (int)o.bookletSubset);
    SetDlgItemInt(hwnd, IDC_PR_SHEETS_FROM, 1, FALSE);
    int nSheets = (d->nPages + 3) / 4;
    SetDlgItemInt(hwnd, IDC_PR_SHEETS_TO, std::max(1, nSheets), FALSE);
    Str binding[] = {Tr("Left"), Tr("Right"), Tr("Left (Tall)"), Tr("Right (Tall)")};
    FillCombo(d, IDC_PR_BINDING, binding, dimof(binding), (int)o.bookletBinding);

    SetChecked(d, IDC_PR_DUPLEX, o.duplex != PrintDuplex::None);
    SetChecked(d, IDC_PR_FLIP_LONG, o.duplex != PrintDuplex::ShortEdge);
    SetChecked(d, IDC_PR_FLIP_SHORT, o.duplex == PrintDuplex::ShortEdge);
    SetOrientation(d, o.orientation);

    Str comments[] = {Tr("Document"), Tr("Document and Markups"), Tr("Document and Stamps"), Tr("Form fields only")};
    int commentsIdx = 1;
    for (int i = 0; i < dimofi(kCommentsModes); i++) {
        if (kCommentsModes[i] == o.annots) {
            commentsIdx = i;
        }
    }
    FillCombo(d, IDC_PR_COMMENTS, comments, dimof(comments), commentsIdx);

    d->updating = false;
}

// Poster, Multiple and Booklet have no Auto orientation: switching to one
// picks the orientation that fits it
static void OnSizingChanged(PrintDlgState* d) {
    PrintSizing sizing = CheckedSizing(d);
    // d->opts still has the sizing from before the click
    if (sizing == PrintSizing::Size || sizing == d->opts.sizing) {
        return;
    }
    bool landscape = true;
    if (sizing == PrintSizing::Poster) {
        SizeF page = d->engine->PageMediabox(d->curPageNo).Size();
        landscape = page.dx > page.dy;
    } else if (sizing == PrintSizing::Multiple) {
        int idx = ComboIdx(d, IDC_PR_PER_SHEET);
        landscape = idx >= kPerSheetCustomIdx || kPerSheetPresets[idx].cols > kPerSheetPresets[idx].rows;
    }
    if (sizing == PrintSizing::Booklet) {
        landscape = ComboIdx(d, IDC_PR_BINDING) < (int)BookletBinding::LeftTall;
    }
    SetOrientation(d, landscape ? PrintOrientation::Landscape : PrintOrientation::Portrait);
}

// a "Pages per sheet" preset shows its columns and rows in the (disabled) boxes
static void ShowPerSheetGrid(PrintDlgState* d) {
    int idx = ComboIdx(d, IDC_PR_PER_SHEET);
    if (idx >= kPerSheetCustomIdx) {
        return;
    }
    d->updating = true;
    SetDlgItemInt(d->hwnd, IDC_PR_COLS_EDIT, kPerSheetPresets[idx].cols, FALSE);
    SetDlgItemInt(d->hwnd, IDC_PR_ROWS_EDIT, kPerSheetPresets[idx].rows, FALSE);
    d->updating = false;
}

// Print: false keeps the dialog open
static bool OnPrint(PrintDlgState* d) {
    ReadControls(d);
    VecReset(d->ranges);
    if (!GetRanges(d, d->ranges)) {
        MessageBoxWarning(d->hwnd, Tr("Enter pages like 1-3, 5 within the document."), Tr("Invalid page range"));
        SetFocus(Item(d, IDC_PR_PAGES_EDIT));
        return false;
    }
    if (!d->printer || !d->devMode) {
        MessageBoxWarning(d->hwnd, Tr("Couldn't initialize printer"), Tr("Printing problem."));
        return false;
    }
    for (PRINTPAGERANGE& r : d->ranges) {
        logf("PrintDialog: printing pages %d-%d\n", (int)r.nFromPage, (int)r.nToPage);
    }
    d->printToFile = IsChecked(d, IDC_PR_TO_FILE);
    if (d->printToFile && !AskOutputFile(d)) {
        return false;
    }
    gLastJob = d->opts;
    gHasLastJob = true;
    gLastMoreOptions = d->moreOptions;
    str::ReplaceWithCopy(&gLastPrinterName, d->printer->name);
    free(gLastDevMode);
    gLastDevMode = DupDevMode(d->devMode);
    return true;
}

static void OpenPrintHelp() {
    SumatraLaunchBrowser(DocURIToWebUrlTemp(StrL("Printing")));
}

static void OnCommand(PrintDlgState* d, int id, int code) {
    switch (id) {
        case IDOK:
            if (OnPrint(d)) {
                EndDialog(d->hwnd, IDOK);
            }
            return;
        case IDCANCEL:
            EndDialog(d->hwnd, IDCANCEL);
            return;
        case IDC_PR_PRINTER:
            if (code == CBN_SELCHANGE) {
                int idx = ComboIdx(d, IDC_PR_PRINTER);
                if (idx < len(d->printerNames)) {
                    LoadPrinter(d, d->printerNames.At(idx));
                }
                UpdatePreview(d);
            }
            return;
        case IDC_PR_PAPER_SIZE:
        case IDC_PR_SOURCE:
        case IDC_PR_QUALITY:
            if (code == CBN_SELCHANGE) {
                OnPaperOptionChanged(d, id);
                UpdatePreview(d);
            }
            return;
        case IDC_PR_PROPERTIES:
            OnPrinterProperties(d);
            UpdatePreview(d);
            return;
        case IDC_PR_HELP_ICON:
            if (code == STN_CLICKED) {
                OpenPrintHelp();
            }
            return;
        case IDC_PR_MORE_ARROW:
        case IDC_PR_MORE:
            if (code == STN_CLICKED) {
                d->moreOptions = !d->moreOptions;
                UpdatePreview(d);
            }
            return;
        case IDC_PR_PREV:
        case IDC_PR_NEXT:
            d->previewIdx += id == IDC_PR_PREV ? -1 : 1;
            UpdatePreviewLabels(d);
            return;
        case IDC_PR_SIZE:
        case IDC_PR_POSTER:
        case IDC_PR_MULTIPLE:
        case IDC_PR_BOOKLET:
            OnSizingChanged(d);
            break;
        case IDC_PR_BINDING:
            // tall bindings fold the paper top / bottom: portrait paper
            if (code == CBN_SELCHANGE) {
                bool tall = ComboIdx(d, IDC_PR_BINDING) >= (int)BookletBinding::LeftTall;
                SetOrientation(d, tall ? PrintOrientation::Portrait : PrintOrientation::Landscape);
            }
            break;
        case IDC_PR_PER_SHEET:
            if (code == CBN_SELCHANGE) {
                ShowPerSheetGrid(d);
            }
            break;
        case IDC_PR_COPIES:
            // Collate shows up for more than one copy
            if (code == EN_CHANGE && !d->updating) {
                LayoutDialog(d);
            }
            return;
    }
    // everything else changes what gets printed
    if (code == BN_CLICKED || code == EN_CHANGE || code == CBN_SELCHANGE) {
        UpdatePreview(d);
    }
}

static INT_PTR CALLBACK PrintDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = (PrintDlgState*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
    switch (msg) {
        case WM_INITDIALOG:
            d = (PrintDlgState*)lp;
            d->hwnd = hDlg;
            SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)d);
            // creating the controls sends notifications: ignored until all exist
            d->updating = true;
            CreateDlgControls(hDlg, kPrintCtrls, dimofi(kPrintCtrls));
            InitControls(d);
            DarkModeApplyToWindow(hDlg);
            UpdatePreview(d);
            HwndCenterDialog(hDlg, GetParent(hDlg));
            SetFocus(Item(d, IDOK));
            return FALSE;

        case WM_COMMAND:
            if (d) {
                OnCommand(d, LOWORD(wp), HIWORD(wp));
            }
            return TRUE;

        case WM_HSCROLL:
            if (d && (HWND)lp == Item(d, IDC_PR_SLIDER)) {
                int pos = (int)SendMessageW((HWND)lp, TBM_GETPOS, 0, 0);
                if (pos != d->previewIdx) {
                    d->previewIdx = pos;
                    UpdatePreviewLabels(d);
                }
            }
            return TRUE;

        case WM_NOTIFY: {
            auto* nm = (NMHDR*)lp;
            if (d && nm->idFrom == IDC_PR_HELP && (nm->code == NM_CLICK || nm->code == NM_RETURN)) {
                OpenPrintHelp();
            }
            return FALSE;
        }

        case WM_DRAWITEM:
            if (!d) {
                return FALSE;
            }
            if (wp == IDC_PR_PREVIEW) {
                DrawPreview(d, (DRAWITEMSTRUCT*)lp);
                return TRUE;
            }
            if (wp == IDC_PR_HELP_ICON) {
                DrawRoundIcon((DRAWITEMSTRUCT*)lp, L"?");
                return TRUE;
            }
            return FALSE;
    }
    return FALSE;
}

void ShowPrintDialog(MainWindow* win) {
    DisplayModel* dm = win->AsFixed();
    if (!dm || !dm->GetEngine()) {
        return;
    }
    WindowTab* tab = win->CurrentTab();

    auto* d = new PrintDlgState();
    d->win = win;
    d->dm = dm;
    d->engine = dm->GetEngine();
    d->engine->AddRef();
    d->nPages = dm->PageCount();
    d->curPageNo = dm->CurrentPageNo();
    d->adv = Print_Advanced_Data(PrintRangeAdv::All, PrintScaleAdv::Shrink);
    d->adv.dpiOverride = gSettings->printerDefaults.printDpi;
    d->adv.autoRotate = gSettings->printerDefaults.autoRotate;
    if (gHasLastJob) {
        d->opts = gLastJob;
    } else if (str::EqI(gSettings->printerDefaults.printScale, StrL("fit"))) {
        d->opts.sizeMode = PrintSizeMode::Fit;
    } else if (str::EqI(gSettings->printerDefaults.printScale, StrL("none"))) {
        d->opts.sizeMode = PrintSizeMode::Actual;
    }
    d->opts.copies = 1;
    d->opts.viewPageNo = 0;
    d->moreOptions = gLastMoreOptions;
    d->inches = GetMeasurementSystem() == 1;

    logf("ShowPrintDialog: file='%s' pages=%d\n", d->engine->FilePath(), d->nPages);
    DLGTEMPLATE* tpl = NewDlgTemplate(CWStrTemp(Tr("Print")));
    INT_PTR res = DialogBoxIndirectParamW(nullptr, tpl, win->hwndFrame, PrintDlgProc, (LPARAM)d);
    free(tpl);

    // the dialog pumps messages: the document may be gone
    bool sameDoc = IsMainWindowValidAndNotClosing(win) && win->CurrentTab() == tab && win->AsFixed() == dm;
    if (res == IDOK && sameDoc) {
        ApplyPrintJobToDevMode(d->devMode, d->opts);
        Str outputFile = d->printToFile ? d->outputFile : Str{};
        PrintJobOnThread(win, d->engine, d->printer->name, d->devMode, d->ranges, d->adv, d->opts, outputFile);
    }
    SafeEngineRelease(&d->engine);
    delete d;
}
