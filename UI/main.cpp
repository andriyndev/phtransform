#include <wx/wxprec.h>
#include <CL/cl.h>
#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <vector>
#include <sstream>

#include "ph_transform_chart.h"

class PITransformUIApp : public wxApp
{
public:
	virtual bool OnInit();
};

class MainFrame : public wxFrame
{
public:
	MainFrame();
	~MainFrame();
};

class ChartFrame : public wxFrame
{
public:
	ChartFrame();
};

enum
{
	BUTTON_LoadFile = wxID_HIGHEST + 1,
	BUTTON_ShowChart,
	PANEL_Image,
	STATIC_TEXT_Result,
	STATIC_TEXT_Time,
	CHOICE_Platforms,
	CHOICE_Devices,
};

wxIMPLEMENT_APP(PITransformUIApp);

wxImage image;
wxPanel *imagePanel;
wxButton *loadFileButton;
wxButton *showChartButton;
wxChoice *platformsList;
wxChoice *devicesList;
const wxSize imagePanelSize = { 100, 100 };
MainFrame *frame;
struct PH_Chart *chart;
wxPoint chartBeginCoords = { -10, -10 };
const wxSize chartUnitSize = { 5, 11 };
const wxPoint triangle_intermediate[] = { wxPoint(chartUnitSize.x * 3, chartUnitSize.y * 2 / 10), wxPoint(chartUnitSize.x * 4, chartUnitSize.y * 5 / 10), wxPoint(chartUnitSize.x * 3,  chartUnitSize.y * 8 / 10) };
const wxPoint triangle_output[] = { wxPoint(chartUnitSize.x * 3, chartUnitSize.y * 2 / 10), wxPoint(chartUnitSize.x * 4, chartUnitSize.y * 2 / 10), wxPoint(chartUnitSize.x * 7 / 2, chartUnitSize.y * 8 / 10) };
wxTextCtrl *resultField;
wxStaticText *timeField;
bool isImageLoaded = false;
bool isLibLoaded = false;
ph_t_devices *devices = NULL;

ChartFrame *chartFrame;
wxPanel *chartPanel;
bool chartPanel_isMousePressed = false;
wxPoint prevMousePos;
ph_t_platforms platforms;

void MainFrame_onPaint(wxPaintEvent &evt)
{
	(void)evt;

	wxPaintDC dc(imagePanel);
	if (isImageLoaded) {
		dc.SetUserScale((float)imagePanelSize.GetX() / image.GetWidth(), (float)imagePanelSize.GetY() / image.GetHeight());
		dc.DrawBitmap(image, 0, 0, false);
	}
}

void ChartFrame_onPaint(wxPaintEvent &evt)
{
	//(void)evt;
	struct PH_Chart *drawFromChart = chart;
	wxPoint drawFromCoord = { 0, 0 };
	wxSize chartPanelSize = chartPanel->GetSize();
	wxSize textSize;
	int level = 1;
	int max_deviation;
	wxString text;
	while (drawFromChart != NULL && drawFromCoord.y + chartUnitSize.y * (int)drawFromChart->size < chartBeginCoords.y) {
		drawFromCoord.x += chartUnitSize.x * 3;
		drawFromCoord.y += chartUnitSize.y * drawFromChart->size;
		drawFromChart = drawFromChart->next;
	}
	wxBufferedPaintDC dc(chartPanel);
	dc.Clear();
	while (drawFromChart != NULL && drawFromCoord.y <= chartBeginCoords.y + chartPanelSize.y) {
		max_deviation = 0;
		text = "Level ";
		for (int i = 0; i < drawFromChart->size; i++) {
			if (drawFromChart->array[max_deviation] + (drawFromCoord.x != 0 ? max_deviation : 0) < drawFromChart->array[i] + (drawFromCoord.x != 0 ? i : 0)) {
				max_deviation = i;
			}
			for (int j = 0; j < drawFromChart->array[i]; j++) {
				if (j == 0) {
					dc.SetBrush(*wxBLACK_BRUSH);
				} else {
					dc.SetBrush(*wxGREY_BRUSH);
				}
				dc.SetPen(wxNullPen);
				dc.DrawRectangle(drawFromCoord.x + (drawFromCoord.x != 0 ? i * chartUnitSize.x * 6 : 0) + j * 6 * chartUnitSize.x - chartBeginCoords.x,
						drawFromCoord.y + i * chartUnitSize.y - chartBeginCoords.y, chartUnitSize.x, chartUnitSize.y);
				dc.SetBrush(*wxBLACK_BRUSH);
				if (drawFromCoord.x != 0 && i == 0 && j == 0) {
					dc.DrawPolygon(3, triangle_output,
							drawFromCoord.x + (drawFromCoord.x != 0 ? i * chartUnitSize.x * 6 : 0) + j * 6 * chartUnitSize.x - chartBeginCoords.x,
							drawFromCoord.y + i * chartUnitSize.y - chartBeginCoords.y);
				} else {
					dc.DrawPolygon(3, triangle_intermediate,
							drawFromCoord.x + (drawFromCoord.x != 0 ? i * chartUnitSize.x * 6 : 0) + j * 6 * chartUnitSize.x - chartBeginCoords.x,
							drawFromCoord.y + i * chartUnitSize.y - chartBeginCoords.y);
				}
				dc.DrawRectangle(drawFromCoord.x + (drawFromCoord.x != 0 ? i * chartUnitSize.x * 6 : 0) + (j * 6 + 1) * chartUnitSize.x - chartBeginCoords.x,
						drawFromCoord.y + i * chartUnitSize.y + chartUnitSize.y * 6 / 12 - chartBeginCoords.y,
						chartUnitSize.x * 5,
						chartUnitSize.y * 2 / 12);
			}
		}
		text << level;
		textSize = dc.GetTextExtent(text);
		dc.DrawText(text, drawFromCoord.x + (drawFromChart->array[max_deviation] + max_deviation + 2) * chartUnitSize.x * 6 - chartBeginCoords.x, drawFromCoord.y + (chartUnitSize.y * drawFromChart->size - textSize.y) / 2 - chartBeginCoords.y);
		drawFromCoord.x += chartUnitSize.x * (drawFromCoord.x != 0 ? 9 : 3);
		drawFromCoord.y += chartUnitSize.y * drawFromChart->size;
		drawFromChart = drawFromChart->next;
		level++;
	}
	dc.SetBrush(*wxBLACK_BRUSH);
	dc.DrawRectangle(10, chartPanelSize.y - 80, chartUnitSize.x, chartUnitSize.y);
	text = "Leftmost elements";
	textSize = dc.GetTextExtent(text);
	dc.DrawText(text, 25, chartPanelSize.y - 80 + (chartUnitSize.y - textSize.y) / 2);
	dc.SetBrush(*wxGREY_BRUSH);
	dc.DrawRectangle(10, chartPanelSize.y - 60, chartUnitSize.x, chartUnitSize.y);
	text = "Other elements";
	textSize = dc.GetTextExtent(text);
	dc.DrawText(text, 25, chartPanelSize.y - 60 + (chartUnitSize.y - textSize.y) / 2);
	dc.SetBrush(*wxBLACK_BRUSH);
	dc.DrawPolygon(3, triangle_intermediate, 10 - triangle_intermediate[0].x, chartPanelSize.y - 40);
	text = "Intermediate results";
	textSize = dc.GetTextExtent(text);
	dc.DrawText(text, 25, chartPanelSize.y - 40 + (chartUnitSize.y - textSize.y) / 2);
	dc.DrawPolygon(3, triangle_output, 10 - triangle_output[0].x, chartPanelSize.y - 20);
	text = "Output elements";
	textSize = dc.GetTextExtent(text);
	dc.DrawText(text, 25, chartPanelSize.y - 20 + (chartUnitSize.y - textSize.y) / 2);
}

bool calculatePHTransform(wxImage *image)
{
	size_t i, j;
	unsigned char *data = image->GetData();
	struct PH_Matrix *matrix;
	struct PH_Array *result;
	size_t height, width;
	wxLongLong startTime;
	char tempBuf[100];

	height = image->GetHeight();
	width = image->GetWidth();
	matrix = (PH_Matrix *)malloc(sizeof(*matrix) + sizeof(matrix->array[0]) * height * width);
	if (matrix == NULL) {
		return false;
	}
	matrix->height = height;
	matrix->width = width;
	for (i = 0; i < height; i++) {
		for (j = 0; j < width; j++) {
			matrix->array[i * width + j] = ((uint64_t)*data + *(data + 1) + *(data + 2)) / 3;
			data += 3;
		}
	}

	if (chart != NULL) {
		ph_transform_free_chart(chart);
	}

	startTime = wxGetUTCTimeMillis();
	result = ph_transform_calculate_with_chart(matrix, &chart);
	snprintf(tempBuf, sizeof(tempBuf), "Time of execution = %lld", wxGetUTCTimeMillis() - startTime);
	timeField->SetLabel(tempBuf);
	free(matrix);

	if (result == NULL) {
		return false;
	}

	resultField->ChangeValue("");
	std::ostringstream stream("");
	for (i = 0; i < result->size; i++) {
		stream << (long)result->array[i];
		if (i != result->size - 1) {
			stream << ", ";
		}
	}
	resultField->ChangeValue(stream.str());

	ph_transform_free_result_mem(result);
	return true;
}

void LoadImageButton_onClick(wxCommandEvent &event)
{
	bool res;

	wxFileDialog openFileDialog(NULL, _("Open image"), "", "",
		"Image files (*.png)|*.png", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (openFileDialog.ShowModal() == wxID_CANCEL)
		return;
	res = image.LoadFile(openFileDialog.GetPath());
	if (!res) {
		wxMessageBox("Failed to load image.", "Error", wxOK | wxICON_ERROR);
		return;
	}
	isImageLoaded = true;
	imagePanel->Refresh();

	calculatePHTransform(&image);
}

void ShowChartButton_onClick(wxCommandEvent &event)
{
	chartFrame = new ChartFrame();
	chartFrame->Show(true);
}

void ChartPanel_onMouseDown(wxMouseEvent &event)
{
	chartPanel_isMousePressed = true;
	prevMousePos = event.GetPosition();
}

void ChartPanel_onMouseUp(wxMouseEvent &event)
{
	chartPanel_isMousePressed = false;
}

void ChartPanel_onMouseMove(wxMouseEvent &event)
{
	if (chartPanel_isMousePressed) {
		chartBeginCoords += prevMousePos - event.GetPosition();
		prevMousePos = event.GetPosition();
		chartPanel->Refresh();
	}
}

void PlatformsList_onSelect(wxCommandEvent &event)
{
	int res;
	uint32_t devices_cnt;
	wxArrayString devicesAS;
	char *str;

	if (devices != NULL) {
		ph_transform_free_available_devices_list(devices);
		devices = NULL;
	}

	res = ph_transform_get_available_devices(platforms, event.GetSelection(), &devices, &devices_cnt);
	if (res < 0) {
		wxMessageBox("Failed to get available devices.", "Error");
		frame->Close();
	}
	for (uint32_t i = 0; i < devices_cnt; i++) {
		res = ph_transform_get_device_name(devices, i, &str);
		if (res < 0) {
			wxMessageBox("Failed to get device name.", "Error");
			frame->Close();
		}
		devicesAS.Add(str);
		ph_transform_free_device_name(str);
	}
	devicesList->Clear();
	devicesList->Append(devicesAS);
	devicesList->Enable(true);
	loadFileButton->Enable(false);
}

void DevicesList_onSelect(wxCommandEvent &event)
{
	int res;

	if (isLibLoaded) {
		ph_transform_fini();
	}
	res = ph_transform_init(devices, devicesList->GetSelection());
	if (res < 0) {
		wxMessageBox("Failed to load PH transform library.", "Error");
		isLibLoaded = false;
		frame->Close();
	}
	else {
		isLibLoaded = true;
	}

	loadFileButton->Enable(true);
}

bool PITransformUIApp::OnInit()
{
	wxInitAllImageHandlers();
	frame = new MainFrame();
	frame->SetClientSize(wxSize(360, 380));
	frame->Show(true);
	return true;
}

MainFrame::MainFrame() : wxFrame(NULL, wxID_ANY, "Parallel-hierarchical transformation", wxDefaultPosition, wxSize(360, 330))
{
	int res;

	uint32_t platformsCnt;
	wxArrayString platformsAS;
	char *str;

	loadFileButton = new wxButton(this, BUTTON_LoadFile, wxT("Load File"), wxPoint(10, 90), wxSize(200, 30));
	res = ph_transform_get_available_platforms(&platforms, &platformsCnt);
	if (res < 0) {
		wxMessageBox("Failed to get available platforms.", "Error");
		frame->Close();
	}
	for (uint32_t i = 0; i < platformsCnt; i++) {
		res = ph_transform_get_platform_name(platforms, i, &str);
		if (res < 0) {
			wxMessageBox("Failed to get platform name.", "Error");
			frame->Close();
		}
		platformsAS.Add(str);
		ph_transform_free_platform_name(str);
	}
	platformsList = new wxChoice(this, CHOICE_Platforms, wxPoint(10, 10), wxSize(200, 30), platformsAS);
	//platformsList->SetLabelText("Select platform name");
	platformsList->Bind(wxEVT_CHOICE, PlatformsList_onSelect);
	devicesList = new wxChoice(this, CHOICE_Devices, wxPoint(10, 50), wxSize(200, 30), NULL);
	devicesList->Bind(wxEVT_CHOICE, DevicesList_onSelect);
	devicesList->Enable(false);
	loadFileButton->Bind(wxEVT_BUTTON, LoadImageButton_onClick);
	loadFileButton->Enable(false);
	showChartButton = new wxButton(this, BUTTON_ShowChart, wxT("ShowChart"), wxPoint(10, 130), wxSize(200, 30));
	showChartButton->Bind(wxEVT_BUTTON, ShowChartButton_onClick);
	imagePanel = new wxPanel(this, PANEL_Image, wxPoint(250, 10), imagePanelSize);
	imagePanel->Bind(wxEVT_PAINT, MainFrame_onPaint);
	resultField = new wxTextCtrl(this, STATIC_TEXT_Result, wxT(""), wxPoint(10, 210), wxSize(340, 200), wxTE_MULTILINE);
	timeField = new wxStaticText(this, STATIC_TEXT_Time, wxT(""), wxPoint(10, 170), wxSize(200, 30), wxALIGN_CENTER_HORIZONTAL);
}

ChartFrame::ChartFrame() : wxFrame(frame, wxID_ANY, "Chart", wxDefaultPosition, wxSize(640, 480))
{
	chartPanel = new wxPanel(this, PANEL_Image);
	chartPanel->Bind(wxEVT_PAINT, ChartFrame_onPaint);
	chartPanel->Bind(wxEVT_LEFT_DOWN, ChartPanel_onMouseDown);
	chartPanel->Bind(wxEVT_LEFT_UP, ChartPanel_onMouseUp);
	chartPanel->Bind(wxEVT_MOTION, ChartPanel_onMouseMove);
	chartPanel->SetDoubleBuffered(true);
	chartPanel->SetCursor(*wxCROSS_CURSOR);
}

MainFrame::~MainFrame()
{
	if (isLibLoaded) {
		ph_transform_fini();
	}

	if (platforms != NULL) {
		ph_transform_free_available_platforms_list(platforms);
	}

	if (devices != NULL) {
		ph_transform_free_available_devices_list(devices);
	}

	if (chart != NULL) {
		ph_transform_free_chart(chart);
	}
}