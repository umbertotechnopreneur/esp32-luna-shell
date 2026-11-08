# Cody Dashboard Sans

Independent Home dashboard fonts derived from the repository's Telex Regular
source, copyright 2011 The Telex Project Authors. The derived family is renamed
**Cody Dashboard Sans** and distributed under the included SIL OFL 1.1.
Existing title, body and other scene typography remain unchanged.

Generate through `scripts/device.ps1 generate -Generator cody-home-dashboard-fonts`.
The fixed source is the vendored `Telex-Regular.ttf`; no installed font or fallback
is selected. The generated manifest records its SHA-256 and each coverage hash.

`CODY::HomeFonts::small_font` contains 14px printable ASCII, while `heading_font`
contains 20px printable ASCII for complete Home status headings. `value_font` contains
32px ASCII `.` through `K`, a contiguous native range including decimal digits,
periods and the thousands suffix. All three use measured fixed cells and 4bpp coverage
with the existing native renderer. Generation does not validate device rendering,
firmware footprint or stack headroom.
