/*
 * chart_sales.h: the chart the deck chart tests share (Presentation gap 2): columns of two series over three
 * quarters as PowerPoint writes its part, and check_sales, which says it was read so. Included once by each test,
 * after its CHECK.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
/* Columns of two series over three quarters, as PowerPoint writes a chart part (with an embedded workbook's
 * cells in <c:f>, and their caches). */
static const char chart_xml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
    "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
    "<c:chart><c:title><c:tx><c:rich><a:bodyPr/><a:p><a:r><a:t>Sales</a:t></a:r></a:p></c:rich></c:tx></c:title>"
    "<c:autoTitleDeleted val=\"0\"/><c:plotArea><c:layout/>"
    "<c:barChart><c:barDir val=\"col\"/><c:grouping val=\"clustered\"/><c:varyColors val=\"0\"/>"
    "<c:ser><c:idx val=\"0\"/><c:order val=\"0\"/>"
    "<c:tx><c:strRef><c:f>Sheet1!$B$1</c:f><c:strCache><c:ptCount val=\"1\"/><c:pt idx=\"0\"><c:v>North</c:v></c:pt>"
    "</c:strCache></c:strRef></c:tx>"
    "<c:cat><c:strRef><c:f>Sheet1!$A$2:$A$4</c:f><c:strCache><c:ptCount val=\"3\"/>"
    "<c:pt idx=\"0\"><c:v>Q1</c:v></c:pt><c:pt idx=\"1\"><c:v>Q2</c:v></c:pt><c:pt idx=\"2\"><c:v>Q3</c:v></c:pt>"
    "</c:strCache></c:strRef></c:cat>"
    "<c:val><c:numRef><c:f>Sheet1!$B$2:$B$4</c:f><c:numCache><c:formatCode>General</c:formatCode><c:ptCount val=\"3\"/>"
    "<c:pt idx=\"0\"><c:v>4.3</c:v></c:pt><c:pt idx=\"1\"><c:v>2.5</c:v></c:pt><c:pt idx=\"2\"><c:v>3.5</c:v></c:pt>"
    "</c:numCache></c:numRef></c:val></c:ser>"
    "<c:ser><c:idx val=\"1\"/><c:order val=\"1\"/>"
    "<c:tx><c:strRef><c:f>Sheet1!$C$1</c:f><c:strCache><c:ptCount val=\"1\"/><c:pt idx=\"0\"><c:v>South</c:v></c:pt>"
    "</c:strCache></c:strRef></c:tx>"
    "<c:cat><c:strRef><c:f>Sheet1!$A$2:$A$4</c:f><c:strCache><c:ptCount val=\"3\"/>"
    "<c:pt idx=\"0\"><c:v>Q1</c:v></c:pt><c:pt idx=\"1\"><c:v>Q2</c:v></c:pt><c:pt idx=\"2\"><c:v>Q3</c:v></c:pt>"
    "</c:strCache></c:strRef></c:cat>"
    "<c:val><c:numRef><c:f>Sheet1!$C$2:$C$4</c:f><c:numCache><c:formatCode>General</c:formatCode><c:ptCount val=\"3\"/>"
    "<c:pt idx=\"0\"><c:v>2.4</c:v></c:pt><c:pt idx=\"2\"><c:v>1.8</c:v></c:pt>"
    "</c:numCache></c:numRef></c:val></c:ser>"
    "<c:axId val=\"1\"/><c:axId val=\"2\"/></c:barChart>"
    "<c:catAx><c:axId val=\"1\"/><c:crossAx val=\"2\"/></c:catAx><c:valAx><c:axId val=\"2\"/><c:crossAx val=\"1\"/>"
    "<c:majorGridlines/></c:valAx></c:plotArea>"
    "<c:legend><c:legendPos val=\"b\"/></c:legend><c:plotVisOnly val=\"1\"/></c:chart></c:chartSpace>";

static int near(double a, double b) { return a - b < 1e-9 && b - a < 1e-9; }

/* The chart above, as read: columns, "Sales", the legend at the bottom, two series of three quarters. */
static void check_sales(const owf_chart *c, const char *what)
{
    CHECK(c->kind == OWF_CHART_COLUMN && c->grouping == OWF_CHART_CLUSTERED, "%s: kind %d %d", what, c->kind,
          c->grouping);
    CHECK(c->title && !strcmp(c->title, "Sales"), "%s: title [%s]", what, c->title ? c->title : "(none)");
    CHECK(c->legend == 'b', "%s: legend %d", what, c->legend);
    CHECK(c->nseries == 2, "%s: %d series", what, c->nseries);
    if (c->nseries != 2) return;
    CHECK(c->series[0].name && !strcmp(c->series[0].name, "North") && c->series[1].name &&
          !strcmp(c->series[1].name, "South"), "%s: series names", what);
    CHECK(c->series[0].npoints == 3 && c->series[1].npoints == 3, "%s: points %d %d", what, c->series[0].npoints,
          c->series[1].npoints);
    if (c->series[0].npoints != 3 || c->series[1].npoints != 3) return;
    CHECK(c->series[0].points[0].label && !strcmp(c->series[0].points[0].label, "Q1") && c->series[0].points[2].label &&
          !strcmp(c->series[0].points[2].label, "Q3"), "%s: categories", what);
    CHECK(c->series[0].points[0].has_value && near(c->series[0].points[0].value, 4.3) &&
          near(c->series[0].points[1].value, 2.5) && near(c->series[0].points[2].value, 3.5), "%s: North's values", what);
    CHECK(c->series[1].points[0].has_value && near(c->series[1].points[0].value, 2.4) && !c->series[1].points[1].has_value &&
          c->series[1].points[2].has_value && near(c->series[1].points[2].value, 1.8), "%s: South's values, a gap in Q2",
          what);
}
