/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

struct MainWindow;

// The custom print dialog: printer, pages, sizing (Size, Poster, Multiple,
// Booklet), two-sided, orientation, comments and a live preview. Prints on a
// background thread when the user clicks Print.
void ShowPrintDialog(MainWindow* win);
