#include "platform.h" // Should be kept first
#include "BoardView.h"
#include "history.h"
#include "utf8/utf8.h"
#include "utils.h"
#include "version.h"

#include <cmath>
#include <iostream>
#include <climits>
#include <memory>
#include <cstdio>
#ifdef ENABLE_SDL2
#include <SDL.h>
#endif
#include <cmath>

#include "BRDBoard.h"
#include "Board.h"
#include "FileFormats/BVR3File.h"
#include "annotations.h"
#include "imgui/imgui.h"
#include "imgui/misc/cpp/imgui_stdlib.h"

#include "NetList.h"
#include "PartList.h"
#include "vectorhulls.h"

#include "../linalg.hpp"

using namespace std;
using namespace std::placeholders;

#if _MSC_VER
#define stricmp _stricmp
#endif

#ifndef _WIN32
#define stricmp strcasecmp
#endif

BoardView::~BoardView() {
	if (m_validBoard) {
		m_board->Nets().clear();
		m_board->Pins().clear();
		m_board->Components().clear();
		m_board->OutlinePoints().clear();
		m_board->OutlineSegments().clear();
		delete m_file;
		delete m_board;
		m_annotations.Close();
		m_validBoard = false;
	}
}
uint32_t BoardView::byte4swap(uint32_t x) {
	/*
	 * used to convert RGBA -> ABGR etc
	 */
	return (((x & 0x000000ff) << 24) | ((x & 0x0000ff00) << 8) | ((x & 0x00ff0000) >> 8) | ((x & 0xff000000) >> 24));
}
void BoardView::ThemeSetStyle(const char *name) {
	ImGuiStyle &style = ImGui::GetStyle();

	// non color-related settings
	style.WindowBorderSize = 0.0f;

	if (strcmp(name, "dark") == 0) {
		style.Colors[ImGuiCol_Text]          = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
		style.Colors[ImGuiCol_TextDisabled]  = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
		style.Colors[ImGuiCol_WindowBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.70f);
		style.Colors[ImGuiCol_ChildBg]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_PopupBg]       = ImVec4(0.05f, 0.05f, 0.10f, 0.90f);
		style.Colors[ImGuiCol_Border]        = ImVec4(0.70f, 0.70f, 0.70f, 0.65f);
		style.Colors[ImGuiCol_BorderShadow]  = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_FrameBg] =
		    ImVec4(0.30f, 0.30f, 0.30f, 1.0f); // Background of checkbox, radio button, plot, slider, text input
		style.Colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.90f, 0.80f, 0.80f, 0.40f);
		style.Colors[ImGuiCol_FrameBgActive]        = ImVec4(0.90f, 0.65f, 0.65f, 0.45f);
		style.Colors[ImGuiCol_TitleBg]              = ImVec4(0.27f, 0.27f, 0.54f, 0.83f);
		style.Colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.40f, 0.40f, 0.80f, 0.20f);
		style.Colors[ImGuiCol_TitleBgActive]        = ImVec4(0.32f, 0.32f, 0.63f, 0.87f);
		style.Colors[ImGuiCol_MenuBarBg]            = ImVec4(0.40f, 0.40f, 0.55f, 0.80f);
		style.Colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.20f, 0.25f, 0.30f, 0.60f);
		style.Colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.40f, 0.40f, 0.80f, 0.30f);
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.80f, 0.40f);
		style.Colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.80f, 0.50f, 0.50f, 0.40f);
		style.Colors[ImGuiCol_CheckMark]            = ImVec4(0.90f, 0.90f, 0.90f, 0.50f);
		style.Colors[ImGuiCol_SliderGrab]           = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
		style.Colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.80f, 0.50f, 0.50f, 1.00f);
		style.Colors[ImGuiCol_Button]               = ImVec4(0.67f, 0.40f, 0.40f, 0.60f);
		style.Colors[ImGuiCol_ButtonHovered]        = ImVec4(0.67f, 0.40f, 0.40f, 1.00f);
		style.Colors[ImGuiCol_ButtonActive]         = ImVec4(0.80f, 0.50f, 0.50f, 1.00f);
		style.Colors[ImGuiCol_Header]               = ImVec4(0.40f, 0.40f, 0.90f, 0.45f);
		style.Colors[ImGuiCol_HeaderHovered]        = ImVec4(0.45f, 0.45f, 0.90f, 0.80f);
		style.Colors[ImGuiCol_HeaderActive]         = ImVec4(0.53f, 0.53f, 0.87f, 0.80f);
		style.Colors[ImGuiCol_Separator]            = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
		style.Colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.70f, 0.60f, 0.60f, 1.00f);
		style.Colors[ImGuiCol_SeparatorActive]      = ImVec4(0.90f, 0.70f, 0.70f, 1.00f);
		style.Colors[ImGuiCol_ResizeGrip]           = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
		style.Colors[ImGuiCol_ResizeGripHovered]    = ImVec4(1.00f, 1.00f, 1.00f, 0.60f);
		style.Colors[ImGuiCol_ResizeGripActive]     = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
		style.Colors[ImGuiCol_PlotLines]            = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		style.Colors[ImGuiCol_PlotLinesHovered]     = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogram]        = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.00f, 0.00f, 1.00f, 0.35f);
		style.Colors[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
		style.Colors[ImGuiCol_TableRowBg]           = ImVec4(0.05f, 0.05f, 0.10f, 0.90f);
		style.Colors[ImGuiCol_TableRowBgAlt]        = ImVec4(0.10f, 0.10f, 0.20f, 0.90f);

		m_colors.backgroundColor          = byte4swap(0x000000ff);
		m_colors.boardFillColor           = byte4swap(0x2a2a2aff);
		m_colors.boardOutlineColor        = byte4swap(0xcc2222ff);
		m_colors.partHullColor            = byte4swap(0x80808080);
		m_colors.partOutlineColor         = byte4swap(0x999999ff);
		m_colors.partFillColor            = byte4swap(0x111111ff);
		m_colors.partTextColor            = byte4swap(0x80808080);
		m_colors.partHighlightedColor     = byte4swap(0xffffffff);
		m_colors.partHighlightedFillColor = byte4swap(0x333333ff);
		m_colors.partHighlightedTextColor            = byte4swap(0x000000ff);
		m_colors.partHighlightedTextBackgroundColor  = byte4swap(0xcccc22ff);
		m_colors.pinDefaultColor          = byte4swap(0x4040ffff);
		m_colors.pinDefaultTextColor      = byte4swap(0xccccccff);
		m_colors.pinTextBackgroundColor   = byte4swap(0xffffff80);
		m_colors.pinGroundColor           = byte4swap(0x5f5f5fff);
		m_colors.pinNotConnectedColor     = byte4swap(0x483D8Bff);
		m_colors.pinTestPadColor          = byte4swap(0x888888ff);
		m_colors.pinTestPadFillColor      = byte4swap(0x6c5b1fff);
		m_colors.pinA1PadColor            = byte4swap(0xdd0000ff);

		m_colors.pinSelectedColor     = byte4swap(0x00ff00ff);
		m_colors.pinSelectedFillColor = byte4swap(0x8888ffff);
		m_colors.pinSelectedTextColor = byte4swap(0xffffffff);

		m_colors.pinSameNetColor     = byte4swap(0x0000ffff);
		m_colors.pinSameNetFillColor = byte4swap(0x9999ffff);
		m_colors.pinSameNetTextColor = byte4swap(0x111111ff);

		m_colors.pinHaloColor     = byte4swap(0xffffff88);
		m_colors.pinNetWebColor   = byte4swap(0xff888888);
		m_colors.pinNetWebOSColor = byte4swap(0x8888ff88);

		m_colors.annotationPartAliasColor       = byte4swap(0xffff00ff);
		m_colors.annotationBoxColor             = byte4swap(0xcccc88ff);
		m_colors.annotationStalkColor           = byte4swap(0xaaaaaaff);
		m_colors.annotationPopupBackgroundColor = byte4swap(0x888888ff);
		m_colors.annotationPopupTextColor       = byte4swap(0xffffffff);

		m_colors.selectedMaskPins    = byte4swap(0xffffffff);
		m_colors.selectedMaskParts   = byte4swap(0xffffffff);
		m_colors.selectedMaskOutline = byte4swap(0xffffffff);

		m_colors.orMaskPins    = byte4swap(0x0);
		m_colors.orMaskParts   = byte4swap(0x0);
		m_colors.orMaskOutline = byte4swap(0x0);

	} else {
		// light theme default
		style.Colors[ImGuiCol_Text]                 = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_TextDisabled]         = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
		style.Colors[ImGuiCol_PopupBg]              = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
		style.Colors[ImGuiCol_WindowBg]             = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
		style.Colors[ImGuiCol_ChildBg]              = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_Border]               = ImVec4(0.00f, 0.00f, 0.00f, 0.39f);
		style.Colors[ImGuiCol_BorderShadow]         = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
		style.Colors[ImGuiCol_FrameBg]              = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		style.Colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
		style.Colors[ImGuiCol_FrameBgActive]        = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
		style.Colors[ImGuiCol_TitleBg]              = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
		style.Colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(1.00f, 1.00f, 1.00f, 0.51f);
		style.Colors[ImGuiCol_TitleBgActive]        = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);
		style.Colors[ImGuiCol_MenuBarBg]            = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.98f, 0.98f, 0.98f, 0.53f);
		style.Colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.69f, 0.69f, 0.69f, 0.80f);
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.49f, 0.49f, 0.49f, 0.80f);
		style.Colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.49f, 0.49f, 0.49f, 1.00f);
		style.Colors[ImGuiCol_CheckMark]            = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_SliderGrab]           = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
		style.Colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_Button]               = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
		style.Colors[ImGuiCol_ButtonHovered]        = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_ButtonActive]         = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_Header]               = ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
		style.Colors[ImGuiCol_HeaderHovered]        = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
		style.Colors[ImGuiCol_HeaderActive]         = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_Separator]            = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
		style.Colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
		style.Colors[ImGuiCol_SeparatorActive]      = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_ResizeGrip]           = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
		style.Colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
		style.Colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
		style.Colors[ImGuiCol_PlotLines]            = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
		style.Colors[ImGuiCol_PlotLinesHovered]     = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogram]        = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
		style.Colors[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
		style.Colors[ImGuiCol_TableRowBg]           = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
		style.Colors[ImGuiCol_TableRowBgAlt]        = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);

		m_colors.backgroundColor          = byte4swap(0xffffffff);
		m_colors.boardFillColor           = byte4swap(0xddddddff);
		m_colors.partHullColor            = byte4swap(0x80808080);
		m_colors.partOutlineColor         = byte4swap(0x444444ff);
		m_colors.partFillColor            = byte4swap(0xffffff77);
		m_colors.partTextColor            = byte4swap(0x80808080);
		m_colors.partHighlightedColor     = byte4swap(0xff0000ff);
		m_colors.partHighlightedFillColor = byte4swap(0xf0f0f0ff);
		m_colors.partHighlightedTextColor            = byte4swap(0xff3030ff);
		m_colors.partHighlightedTextBackgroundColor  = byte4swap(0xffff00ff);
		m_colors.boardOutlineColor        = byte4swap(0x444444ff);
		m_colors.pinDefaultColor          = byte4swap(0x22aa33ff);
		m_colors.pinDefaultTextColor      = byte4swap(0x666688ff);
		m_colors.pinTextBackgroundColor   = byte4swap(0xffffff80);
		m_colors.pinGroundColor           = byte4swap(0x2222aaff);
		m_colors.pinNotConnectedColor     = byte4swap(0xaaaaaaff);
		m_colors.pinTestPadColor          = byte4swap(0x888888ff);
		m_colors.pinTestPadFillColor      = byte4swap(0xd6c68dff);
		m_colors.pinA1PadColor            = byte4swap(0xdd0000ff);

		m_colors.pinSelectedColor     = byte4swap(0x00000000);
		m_colors.pinSelectedFillColor = byte4swap(0x8888ffff);
		m_colors.pinSelectedTextColor = byte4swap(0xffffffff);

		m_colors.pinSameNetColor     = byte4swap(0x888888ff);
		m_colors.pinSameNetFillColor = byte4swap(0x9999ffff);
		m_colors.pinSameNetTextColor = byte4swap(0x111111ff);

		m_colors.pinHaloColor     = byte4swap(0x22FF2288);
		m_colors.pinNetWebColor   = byte4swap(0xff0000aa);
		m_colors.pinNetWebOSColor = byte4swap(0x0000ff33);

		m_colors.annotationPartAliasColor       = byte4swap(0xffff00ff);
		m_colors.annotationBoxColor             = byte4swap(0xff0000aa);
		m_colors.annotationStalkColor           = byte4swap(0x000000ff);
		m_colors.annotationPopupBackgroundColor = byte4swap(0xeeeeeeff);
		m_colors.annotationPopupTextColor       = byte4swap(0x000000ff);

		m_colors.selectedMaskPins    = byte4swap(0xffffffff);
		m_colors.selectedMaskParts   = byte4swap(0xffffffff);
		m_colors.selectedMaskOutline = byte4swap(0xffffffff);

		m_colors.orMaskPins    = byte4swap(0x0);
		m_colors.orMaskParts   = byte4swap(0x0);
		m_colors.orMaskOutline = byte4swap(0x0);
	}
}

int BoardView::ConfigParse(void) {
	ImGuiStyle &style = ImGui::GetStyle();
	const char *v     = obvconfig.ParseStr("colorTheme", "light");
	ThemeSetStyle(v);

	fontSize            = obvconfig.ParseDouble("fontSize", 20);
	pinSizeThresholdLow = obvconfig.ParseDouble("pinSizeThresholdLow", 0);
	pinShapeSquare      = obvconfig.ParseBool("pinShapeSquare", false);
	pinShapeCircle      = obvconfig.ParseBool("pinShapeCircle", true);

	if ((!pinShapeCircle) && (!pinShapeSquare)) {
		pinShapeSquare = true;
	}

	// Special test here, in case we've already set the dpi from external
	// such as command line.
	if (!dpi) dpi = obvconfig.ParseInt("dpi", 100);
	if (dpi < 50) dpi = 50;
	if (dpi > 400) dpi = 400;
	dpiscale = dpi / 100.0f;

	pinHalo          = obvconfig.ParseBool("pinHalo", true);
	pinHaloDiameter  = obvconfig.ParseDouble("pinHaloDiameter", 1.25);
	pinHaloThickness = obvconfig.ParseDouble("pinHaloThickness", 2.0);
	pinSelectMasks   = obvconfig.ParseBool("pinSelectMasks", true);

	pinA1threshold	  = obvconfig.ParseInt("pinA1threshold", 3);

	showFPS                   = obvconfig.ParseBool("showFPS", false);
	showInfoPanel             = obvconfig.ParseBool("showInfoPanel", true);
	infoPanelSelectPartsOnNet = obvconfig.ParseBool("infoPanelSelectPartsOnNet", true);
	infoPanelCenterZoomNets   = obvconfig.ParseBool("infoPanelCenterZoomNets", true);
	partZoomScaleOutFactor    = obvconfig.ParseDouble("partZoomScaleOutFactor", 3.0f);

	m_info_surface.x          = obvconfig.ParseInt("infoPanelWidth", 350);
	showPins                  = obvconfig.ParseBool("showPins", true);
	showPosition              = obvconfig.ParseBool("showPosition", true);
	showNetWeb                = obvconfig.ParseBool("showNetWeb", true);
	showAnnotations           = obvconfig.ParseBool("showAnnotations", true);
	backgroundImage.enabled   = obvconfig.ParseBool("showBackgroundImage", true);
	fillParts                 = obvconfig.ParseBool("fillParts", true);
	showPartName              = obvconfig.ParseBool("showPartName", true);
	showPinName               = obvconfig.ParseBool("showPinName", true);
	showPartType              = obvconfig.ParseBool("showPartType", true);
	showMode                  = (ShowMode)obvconfig.ParseInt("showMode", ShowMode_Diode);
	m_centerZoomSearchResults = obvconfig.ParseBool("centerZoomSearchResults", true);
	flipMode                  = obvconfig.ParseInt("flipMode", 0);

	boardFill        = obvconfig.ParseBool("boardFill", true);
	boardFillSpacing = obvconfig.ParseInt("boardFillSpacing", 3);

	zoomFactor   = obvconfig.ParseInt("zoomFactor", 10) / 10.0f;
	zoomModifier = obvconfig.ParseInt("zoomModifier", 5);

	panFactor = obvconfig.ParseInt("panFactor", 30);
	panFactor = DPI(panFactor);

	panModifier = obvconfig.ParseInt("panModifier", 5);

	annotationBoxSize = obvconfig.ParseInt("annotationBoxSize", 15);
	annotationBoxSize = DPI(annotationBoxSize);

	annotationBoxOffset = obvconfig.ParseInt("annotationBoxOffset", 8);
	annotationBoxOffset = DPI(annotationBoxOffset);

	netWebThickness = obvconfig.ParseInt("netWebThickness", 2);

	/*
	 * Some machines (Atom etc) don't have enough CPU/GPU
	 * grunt to cope with the large number of AA'd circles
	 * generated on a large dense board like a Macbook Pro
	 * so we have the lowCPU option which will let people
	 * trade good-looks for better FPS
	 *
	 * If we want slowCPU to take impact from a command line
	 * parameter, we need it to be set to false before we
	 * call this.
	 */
	slowCPU |= obvconfig.ParseBool("slowCPU", false);
	style.AntiAliasedLines = !slowCPU;
	style.AntiAliasedFill  = !slowCPU;

	/*
	 * Colours in ImGui can be represented as a 4-byte packed uint32_t as ABGR
	 * but most humans are more accustomed to RBGA, so for the sake of readability
	 * we use the human-readable version but swap the ordering around when
	 * it comes to assigning the actual colour to ImGui.
	 */

	/*
	 * XRayBlue theme
	 */

	m_colors.backgroundColor = byte4swap(obvconfig.ParseHex("backgroundColor", byte4swap(m_colors.backgroundColor)));
	m_colors.boardFillColor  = byte4swap(obvconfig.ParseHex("boardFillColor", byte4swap(m_colors.boardFillColor)));

	m_colors.partHullColor        = byte4swap(obvconfig.ParseHex("partHullColor", byte4swap(m_colors.partHullColor)));
	m_colors.partOutlineColor     = byte4swap(obvconfig.ParseHex("partOutlineColor", byte4swap(m_colors.partOutlineColor)));
	m_colors.partFillColor        = byte4swap(obvconfig.ParseHex("partFillColor", byte4swap(m_colors.partFillColor)));
	m_colors.partTextColor = byte4swap(obvconfig.ParseHex("partTextColor", byte4swap(m_colors.partTextColor)));
	m_colors.partHighlightedColor = byte4swap(obvconfig.ParseHex("partHighlightedColor", byte4swap(m_colors.partHighlightedColor)));
	m_colors.partHighlightedFillColor =
	    byte4swap(obvconfig.ParseHex("partHighlightedFillColor", byte4swap(m_colors.partHighlightedFillColor)));
	m_colors.partHighlightedTextColor = byte4swap(obvconfig.ParseHex("partHighlightedTextColor", byte4swap(m_colors.partHighlightedTextColor)));
	m_colors.partHighlightedTextBackgroundColor =
	    byte4swap(obvconfig.ParseHex("partHighlightedTextBackgroundColor", byte4swap(m_colors.partHighlightedTextBackgroundColor)));
	m_colors.boardOutlineColor    = byte4swap(obvconfig.ParseHex("boardOutlineColor", byte4swap(m_colors.boardOutlineColor)));
	m_colors.pinDefaultColor      = byte4swap(obvconfig.ParseHex("pinDefaultColor", byte4swap(m_colors.pinDefaultColor)));
	m_colors.pinDefaultTextColor  = byte4swap(obvconfig.ParseHex("pinDefaultTextColor", byte4swap(m_colors.pinDefaultTextColor)));
	m_colors.pinTextBackgroundColor  = byte4swap(obvconfig.ParseHex("pinTextBackgroundColor", byte4swap(m_colors.pinTextBackgroundColor)));
	m_colors.pinGroundColor       = byte4swap(obvconfig.ParseHex("pinGroundColor", byte4swap(m_colors.pinGroundColor)));
	m_colors.pinNotConnectedColor = byte4swap(obvconfig.ParseHex("pinNotConnectedColor", byte4swap(m_colors.pinNotConnectedColor)));
	m_colors.pinTestPadColor      = byte4swap(obvconfig.ParseHex("pinTestPadColor", byte4swap(m_colors.pinTestPadColor)));
	m_colors.pinTestPadFillColor  = byte4swap(obvconfig.ParseHex("pinTestPadFillColor", byte4swap(m_colors.pinTestPadFillColor)));
	m_colors.pinA1PadColor        = byte4swap(obvconfig.ParseHex("pinA1PadColor", byte4swap(m_colors.pinA1PadColor)));

	m_colors.pinSelectedTextColor = byte4swap(obvconfig.ParseHex("pinSelectedTextColor", byte4swap(m_colors.pinSelectedTextColor)));
	m_colors.pinSelectedFillColor = byte4swap(obvconfig.ParseHex("pinSelectedFillColor", byte4swap(m_colors.pinSelectedFillColor)));
	m_colors.pinSelectedColor     = byte4swap(obvconfig.ParseHex("pinSelectedColor", byte4swap(m_colors.pinSelectedColor)));

	m_colors.pinSameNetTextColor = byte4swap(obvconfig.ParseHex("pinSameNetTextColor", byte4swap(m_colors.pinSameNetTextColor)));
	m_colors.pinSameNetFillColor = byte4swap(obvconfig.ParseHex("pinSameNetFillColor", byte4swap(m_colors.pinSameNetFillColor)));
	m_colors.pinSameNetColor     = byte4swap(obvconfig.ParseHex("pinSameNetColor", byte4swap(m_colors.pinSameNetColor)));

	m_colors.pinHaloColor   = byte4swap(obvconfig.ParseHex("pinHaloColor", byte4swap(m_colors.pinHaloColor)));
	m_colors.pinNetWebColor = byte4swap(obvconfig.ParseHex("pinNetWebColor", byte4swap(m_colors.pinNetWebColor)));

	m_colors.annotationPartAliasColor =
	    byte4swap(obvconfig.ParseHex("annotationPartAliasColor", byte4swap(m_colors.annotationPartAliasColor)));
	m_colors.annotationBoxColor   = byte4swap(obvconfig.ParseHex("annotationBoxColor", byte4swap(m_colors.annotationBoxColor)));
	m_colors.annotationStalkColor = byte4swap(obvconfig.ParseHex("annotationStalkColor", byte4swap(m_colors.annotationStalkColor)));
	m_colors.annotationPopupBackgroundColor =
	    byte4swap(obvconfig.ParseHex("annotationPopupBackgroundColor", byte4swap(m_colors.annotationPopupBackgroundColor)));
	m_colors.annotationPopupTextColor =
	    byte4swap(obvconfig.ParseHex("annotationPopupTextColor", byte4swap(m_colors.annotationPopupTextColor)));

	m_colors.selectedMaskPins    = byte4swap(obvconfig.ParseHex("selectedMaskPins", byte4swap(m_colors.selectedMaskPins)));
	m_colors.selectedMaskParts   = byte4swap(obvconfig.ParseHex("selectedMaskParts", byte4swap(m_colors.selectedMaskParts)));
	m_colors.selectedMaskOutline = byte4swap(obvconfig.ParseHex("selectedMaskOutline", byte4swap(m_colors.selectedMaskOutline)));

	m_colors.orMaskPins    = byte4swap(obvconfig.ParseHex("orMaskPins", byte4swap(m_colors.orMaskPins)));
	m_colors.orMaskParts   = byte4swap(obvconfig.ParseHex("orMaskParts", byte4swap(m_colors.orMaskParts)));
	m_colors.orMaskOutline = byte4swap(obvconfig.ParseHex("orMaskOutline", byte4swap(m_colors.orMaskOutline)));

	/*
	 * The asus .fz file formats require a specific key to be decoded.
	 *
	 * This key is supplied in the obv.conf file as a long single line
	 * of comma/space separated 32-bit hex values 0x1234abcd etc.
	 *
	 */
	SetFZKey(obvconfig.ParseStr("FZKey", ""));

	keybindings.readFromConfig(obvconfig);

	return 0;
}
static vector<vector<Pin*>> rotateMatrix90Counterclockwise(const vector<vector<Pin*>>& matrix) {
    int m = matrix.size();
    int n = matrix[0].size();
    vector<vector<Pin*>> result(n, vector<Pin*>(m));
    
    for (int i = 0; i < m; ++i) {
        for (int j = 0; j < n; ++j) {
            result[n - 1 - j][i] = matrix[i][j];
        }
    }
    
    return result;
}
void ReloadPinInfos(Annotations &m_annotations, Board *m_board) {
	m_annotations.RefreshPinInfos();
	for (auto &part : m_board->Components()) {
		if (m_annotations.partInfos.count(part->name) == 0) continue;
		auto& partInfo = m_annotations.partInfos[part->name];
		part->set_part_type(partInfo.part_type);
		auto &pins = partInfo.pins;
		for (auto &pin : part->pins) {
			if (pins.count(pin->name) == 0) continue;
			auto &pinInfo = pins[pin->name];
			if (pinInfo.diode.size() > 0) pin->diode_value = pinInfo.diode;
			if (pinInfo.voltage.size() > 0) pin->voltage_value = pinInfo.voltage;
			if (pinInfo.ohm.size() > 0) pin->ohm_value = pinInfo.ohm;
			if (pinInfo.ohm_black.size() > 0) pin->ohm_black_value = pinInfo.ohm_black;
			if (pinInfo.voltage_flag != PinVoltageFlag::unknown) pin->voltage_flag = pinInfo.voltage_flag;
		}
		part->angle = partInfo.angle;
		if (partInfo.angle != PartAngle::unknown) {
			auto& pins = part->pins;
			// build from pins
			auto A1PinIter = std::find_if(pins.cbegin(), pins.cend(), [](auto& p) {
				return p->name == "A1";
			});
			if (A1PinIter != pins.cend()) {
				auto& a1Pin = *A1PinIter;
				auto& max = *std::max_element(pins.cbegin(), pins.cend(), [](auto& l, auto& r){
					return l->name < r->name;
				});

				using namespace linalg::aliases;
				using transformMatrix_t = int3x3;

				transformMatrix_t logicScreenToDataMatrix = {
				    {1, 0, 0},
				    {0, -1, 0},
				    {0, 0, 1},
				};

				auto key = max->name;
				auto m = std::atoi(key.data() + 1); // index begin  1
				auto n = key[0] - 'A' + 1; //index begin 0
				if (key[0] > 'I')
					n--;

				auto aMaxIter = std::find_if(pins.cbegin(), pins.cend(), [t = "A" + std::to_string(m)](auto& p){
					return p->name == t;
				});
				if (aMaxIter == pins.cend()) {
					return;
				}

				auto& aMax = *aMaxIter;
				float2 xAsixVec{a1Pin->position.x - aMax->position.x, a1Pin->position.y - aMax->position.y};
				transformMatrix_t logicScreenToA1LogicMatrix;
				if (abs(xAsixVec.x) > abs(xAsixVec.y)) {
					logicScreenToA1LogicMatrix = {
						{a1Pin->position.x > max->position.x ? -1 : 1, 0, xAsixVec.x > 0 ? n : -n},
						{0, a1Pin->position.y > max->position.y ? -1 : 1, 0},
						{0, 0, 1},
					};
				} else {
					return;
				}
				
				
				auto o1 = linalg::mul(logicScreenToA1LogicMatrix, int3{n, m, 1});
				transformMatrix_t a1LogincToLogicScreenMatrix = linalg::inverse(logicScreenToA1LogicMatrix);
				
				auto orgin = linalg::mul(a1LogincToLogicScreenMatrix, int3{0, 0, 1});
				vector<vector<Pin*>> matrixPin(n, vector<Pin*>(m));
				auto printMatrix = [](const auto& matrix) {
					auto n = matrix.size();
					auto m = matrix[0].size();
					for (int i=0;i<n;i++) {
						for (int j=0;j<m;j++) {
							auto pin = matrix[i][j];
							printf("%s ", pin ? pin->name.c_str(): "__");
						}
						printf("\n");
					}
				};

				for (auto &pin : part->pins) {
					auto& name = pin->name;
					auto m1 = std::atoi(name.data() + 1) - 1;
					auto n1 = name[0] - 'A';
					if (name[0] > 'I')
						n1--;
						
					if (partInfo.angle == PartAngle::_270) {
						
					}
					matrixPin[n1][m1] = pin.get();
				}
				printMatrix(matrixPin);
			}
		}
	}
}

int BoardView::LoadFile(const filesystem::path &filepath) {
	m_lastFileOpenWasInvalid = true;
	m_validBoard             = false;
	if (!filepath.empty()) {
		// clean up the previous file.
		if (m_file && m_board) {
			m_pinHighlighted.clear();
			m_partHighlighted.clear();
			m_annotations.Close();
			m_board->Nets().clear();
			m_board->Pins().clear();
			m_board->Components().clear();
			m_board->OutlinePoints().clear();
			m_board->OutlineSegments().clear();
			//	delete m_file;
			// delete m_board;
		}
		delete m_file;
		m_file = nullptr;
		m_validBoard = false;
		m_error_msg.clear();
		pdfBridge.CloseDocument();

		SetLastFileOpenName(filepath.string());
		std::vector<char> buffer = file_as_buffer(filepath, m_error_msg);
		if (!buffer.empty()) {
			if (BVR3File::verifyFormat(buffer))
				m_file = new BVR3File(buffer);
			else
				m_error_msg = "Unrecognized file format.";

			if (m_file && m_file->valid) {
				LoadBoard(m_file);
				fhistory.Prepend_save(filepath.string());
				history_file_has_changed = 1; // used by main to know when to update the window title
				boardMinMaxDone          = false;
				m_rotation               = 0;
				m_current_side           = kBoardSideTop;
				EPCCheck(); // check to see we don't have a flipped board outline

				m_annotations.SetFilename(filepath.string());
				m_annotations.Load();

				auto conffilepath = filepath;
				conffilepath.replace_extension("conf");
				backgroundImage.loadFromConfig(conffilepath);
				pdfFile.loadFromConfig(conffilepath);

				pdfBridge.OpenDocument(pdfFile);

				/*
				 * Set pins to a known lower size, they get resized
				 * in DrawParts() when the component is analysed
				 */
				for (auto &p : m_board->Pins()) {
					//					auto p      = pin.get();
					if (p->diameter <= 0)
						p->diameter = 7;
				}

				ReloadPinInfos(m_annotations, m_board);

				CenterView();
				m_lastFileOpenWasInvalid = false;
				m_validBoard             = true;
				m_error_msg.clear();
			}
		}
	} else {
		return 1;
	}

	return 0;
}

void BoardView::SetFZKey(const char *keytext) {

	if (keytext) {
		int ki;
		const char *p, *limit;
		char *ep;
		ki    = 0;
		p     = keytext;
		limit = keytext + strlen(keytext);

		if ((limit - p) > 440) {
			/*
			 * we *assume* that the key is correctly formatted in the configuration file
			 * as such it should be like FZKey = 0x12345678, 0xabcd1234, ...
			 *
			 * If your key is incorrectly formatted, or incorrect, it'll cause OBV to
			 * likely crash / segfault (for now).
			 */
			while (p && (p < limit) && ki < 44) {

				// locate the start of the u32 hex value
				while ((p < limit) && (*p != '0')) p++;

				// decode the next number, ep will be set to the end of the converted string
				FZKey[ki] = strtoll(p, &ep, 16);

				ki++;
				p = ep;
			}
		}
	}
}

void RA(const char *t, int w) {
	ImVec2 s = ImGui::CalcTextSize(t);
	s.x      = w - s.x;
	ImGui::Dummy(s);
	ImGui::SameLine();
	ImGui::Text("%s", t);
}

void u32tof4(uint32_t c, float *f) {
	f[0] = (c & 0xff) / 0xff;
	c >>= 8;
	f[1] = (c & 0xff) / 0xff;
	c >>= 8;
	f[2] = (c & 0xff) / 0xff;
	c >>= 8;
	f[3] = (c & 0xff) / 0xff;
}
void BoardView::ColorPreferencesItem(
    const char *label, int label_width, const char *butlabel, const char *conflabel, int var_width, uint32_t *c) {
	std::string desc_id = "##ColorButton" + std::string(label);
	char buf[20];
	snprintf(buf, sizeof(buf), "%08X", byte4swap(*c));
	RA(label, label_width);
	ImGui::SameLine();
	ImGui::ColorButton(desc_id.c_str(), ImColor(*c));
	ImGui::SameLine();
	ImGui::PushItemWidth(var_width);
	if (ImGui::InputText(
	        butlabel, buf, sizeof(buf), ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_CharsHexadecimal, nullptr, buf)) {
		*c = byte4swap(strtoll(buf, NULL, 16));
		snprintf(buf, sizeof(buf), "0x%08x", byte4swap(*c));
		obvconfig.WriteStr(conflabel, buf);
	}
	ImGui::PopItemWidth();
}

void BoardView::SaveAllColors(void) {

	obvconfig.WriteHex("backgroundColor", byte4swap(m_colors.backgroundColor));
	obvconfig.WriteHex("boardFillColor", byte4swap(m_colors.boardFillColor));
	obvconfig.WriteHex("boardOutlineColor", byte4swap(m_colors.boardOutlineColor));
	obvconfig.WriteHex("partOutlineColor", byte4swap(m_colors.partOutlineColor));
	obvconfig.WriteHex("partHullColor", byte4swap(m_colors.partHullColor));
	obvconfig.WriteHex("partFillColor", byte4swap(m_colors.partFillColor));
	obvconfig.WriteHex("partTextColor", byte4swap(m_colors.partTextColor));
	obvconfig.WriteHex("partHighlightedColor", byte4swap(m_colors.partHighlightedColor));
	obvconfig.WriteHex("partHighlightedFillColor", byte4swap(m_colors.partHighlightedFillColor));
	obvconfig.WriteHex("partHighlightedTextColor", byte4swap(m_colors.partHighlightedTextColor));
	obvconfig.WriteHex("partHighlightedTextBackgroundColor", byte4swap(m_colors.partHighlightedTextBackgroundColor));
	obvconfig.WriteHex("pinDefaultColor", byte4swap(m_colors.pinDefaultColor));
	obvconfig.WriteHex("pinDefaultTextColor", byte4swap(m_colors.pinDefaultTextColor));
	obvconfig.WriteHex("pinTextBackgroundColor", byte4swap(m_colors.pinTextBackgroundColor));
	obvconfig.WriteHex("pinGroundColor", byte4swap(m_colors.pinGroundColor));
	obvconfig.WriteHex("pinNotConnectedColor", byte4swap(m_colors.pinNotConnectedColor));
	obvconfig.WriteHex("pinTestPadColor", byte4swap(m_colors.pinTestPadColor));
	obvconfig.WriteHex("pinTestPadFillColor", byte4swap(m_colors.pinTestPadFillColor));
	obvconfig.WriteHex("pinA1PadColor", byte4swap(m_colors.pinA1PadColor));
	obvconfig.WriteHex("pinSelectedColor", byte4swap(m_colors.pinSelectedColor));
	obvconfig.WriteHex("pinSelectedTextColor", byte4swap(m_colors.pinSelectedTextColor));
	obvconfig.WriteHex("pinSelectedFillColor", byte4swap(m_colors.pinSelectedFillColor));
	obvconfig.WriteHex("pinSameNetColor", byte4swap(m_colors.pinSameNetColor));
	obvconfig.WriteHex("pinSameNetTextColor", byte4swap(m_colors.pinSameNetTextColor));
	obvconfig.WriteHex("pinSameNetFillColor", byte4swap(m_colors.pinSameNetFillColor));
	obvconfig.WriteHex("pinHaloColor", byte4swap(m_colors.pinHaloColor));
	obvconfig.WriteHex("pinNetWebColor", byte4swap(m_colors.pinNetWebColor));
	obvconfig.WriteHex("pinNetWebOSColor", byte4swap(m_colors.pinNetWebOSColor));
	obvconfig.WriteHex("annotationPopupTextColor", byte4swap(m_colors.annotationPopupTextColor));
	obvconfig.WriteHex("annotationPopupBackgroundColor", byte4swap(m_colors.annotationPopupBackgroundColor));
	obvconfig.WriteHex("annotationBoxColor", byte4swap(m_colors.annotationBoxColor));
	obvconfig.WriteHex("annotationStalkColor", byte4swap(m_colors.annotationStalkColor));
	obvconfig.WriteHex("selectedMaskOutline", byte4swap(m_colors.selectedMaskOutline));
	obvconfig.WriteHex("selectedMaskParts", byte4swap(m_colors.selectedMaskParts));
	obvconfig.WriteHex("selectedMaskPins", byte4swap(m_colors.selectedMaskPins));
	obvconfig.WriteHex("orMaskPins", byte4swap(m_colors.orMaskPins));
	obvconfig.WriteHex("orMaskParts", byte4swap(m_colors.orMaskParts));
	obvconfig.WriteHex("orMaskOutline", byte4swap(m_colors.orMaskOutline));
}

void BoardView::ColorPreferences(void) {
	bool dummy = true;
	//	ImGui::PushStyleColor(ImGuiCol_PopupBg, 0xffe0e0e0);
	ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), 0, ImVec2(0.5f, 0.5f));;
	if (ImGui::BeginPopupModal("Colour Preferences", &dummy, ImGuiWindowFlags_AlwaysAutoResize)) {

		if (m_showColorPreferences) {
			m_showColorPreferences = false;
			m_tooltips_enabled     = false;
		}

		ImGui::Dummy(ImVec2(1, DPI(5)));
		RA("Base theme", DPI(200));
		ImGui::SameLine();
		{
			int tc        = 0;
			const char *v = obvconfig.ParseStr("colorTheme", "default");
			if (strcmp(v, "dark") == 0) {
				tc = 1;
			}
			if (ImGui::RadioButton("Light", &tc, 0)) {
				obvconfig.WriteStr("colorTheme", "light");
				ThemeSetStyle("light");
				SaveAllColors();
				m_needsRedraw = true;
			}
			ImGui::SameLine();
			if (ImGui::RadioButton("Dark", &tc, 1)) {
				obvconfig.WriteStr("colorTheme", "dark");
				ThemeSetStyle("dark");
				SaveAllColors();
				m_needsRedraw = true;
			}
		}
		ImGui::Dummy(ImVec2(1, DPI(5)));

		ColorPreferencesItem("Background", DPI(200), "##Background", "backgroundColor", DPI(150), &m_colors.backgroundColor);
		ColorPreferencesItem("Board fill", DPI(200), "##Boardfill", "boardFillColor", DPI(150), &m_colors.boardFillColor);
		ColorPreferencesItem(
		    "Board outline", DPI(200), "##BoardOutline", "boardOutlineColor", DPI(150), &m_colors.boardOutlineColor);

		ImGui::Dummy(ImVec2(1, DPI(10)));
		ImGui::Text("Parts");
		ImGui::Separator();
		ColorPreferencesItem("Outline", DPI(200), "##PartOutline", "partOutlineColor", DPI(150), &m_colors.partOutlineColor);
		ColorPreferencesItem("Hull", DPI(200), "##PartHull", "partHullColor", DPI(150), &m_colors.partHullColor);
		ColorPreferencesItem("Fill", DPI(200), "##PartFill", "partFillColor", DPI(150), &m_colors.partFillColor);
		ColorPreferencesItem("Text", DPI(200), "##PartText", "partTextColor", DPI(150), &m_colors.partTextColor);
		ColorPreferencesItem(
		    "Selected", DPI(200), "##PartSelected", "partHighlightedColor", DPI(150), &m_colors.partHighlightedColor);
		ColorPreferencesItem("Fill (selected)",
		                     DPI(200),
		                     "##PartFillSelected",
		                     "partHighlightedFillColor",
		                     DPI(150),
		                     &m_colors.partHighlightedFillColor);
		ColorPreferencesItem("Text (selected)", DPI(200), "##PartHighlightedText", "partHighlightedTextColor", DPI(150), &m_colors.partHighlightedTextColor);
		ColorPreferencesItem("Text background (selected)",
		                     DPI(200),
		                     "##PartHighlightedTextBackground",
		                     "partHighlightedTextBackgroundColor",
		                     DPI(150),
		                     &m_colors.partHighlightedTextBackgroundColor);

		ImGui::Dummy(ImVec2(1, DPI(10)));
		ImGui::Text("Pins");
		ImGui::Separator();
		ColorPreferencesItem("Default", DPI(200), "##PinDefault", "pinDefaultColor", DPI(150), &m_colors.pinDefaultColor);
		ColorPreferencesItem(
		    "Default text", DPI(200), "##PinDefaultText", "pinDefaultTextColor", DPI(150), &m_colors.pinDefaultTextColor);
		ColorPreferencesItem(
		    "Text background", DPI(200), "##PinTextBackground", "pinTextBackgroundColor", DPI(150), &m_colors.pinTextBackgroundColor);
		ColorPreferencesItem("Ground", DPI(200), "##PinGround", "pinGroundColor", DPI(150), &m_colors.pinGroundColor);
		ColorPreferencesItem("NC", DPI(200), "##PinNC", "pinNotConnectedColor", DPI(150), &m_colors.pinNotConnectedColor);
		ColorPreferencesItem("Test pad", DPI(200), "##PinTP", "pinTestPadColor", DPI(150), &m_colors.pinTestPadColor);
		ColorPreferencesItem("Test pad fill", DPI(200), "##PinTPF", "pinTestPadFillColor", DPI(150), &m_colors.pinTestPadFillColor);
		ColorPreferencesItem("A1/1 Pad", DPI(200), "##PinA1", "pinA1PadColor", DPI(150), &m_colors.pinA1PadColor);

		ColorPreferencesItem("Selected", DPI(200), "##PinSelectedColor", "pinSelectedColor", DPI(150), &m_colors.pinSelectedColor);
		ColorPreferencesItem(
		    "Selected fill", DPI(200), "##PinSelectedFillColor", "pinSelectedFillColor", DPI(150), &m_colors.pinSelectedFillColor);
		ColorPreferencesItem(
		    "Selected text", DPI(200), "##PinSelectedTextColor", "pinSelectedTextColor", DPI(150), &m_colors.pinSelectedTextColor);

		ColorPreferencesItem("Same Net", DPI(200), "##PinSameNetColor", "pinSameNetColor", DPI(150), &m_colors.pinSameNetColor);
		ColorPreferencesItem(
		    "SameNet fill", DPI(200), "##PinSameNetFillColor", "pinSameNetFillColor", DPI(150), &m_colors.pinSameNetFillColor);
		ColorPreferencesItem(
		    "SameNet text", DPI(200), "##PinSameNetTextColor", "pinSameNetTextColor", DPI(150), &m_colors.pinSameNetTextColor);

		ColorPreferencesItem("Halo", DPI(200), "##PinHalo", "pinHaloColor", DPI(150), &m_colors.pinHaloColor);
		ColorPreferencesItem("Net web strands", DPI(200), "##NetWebStrands", "pinNetWebColor", DPI(150), &m_colors.pinNetWebColor);
		ColorPreferencesItem(
		    "Net web (otherside)", DPI(200), "##NetWebOSStrands", "pinNetWebOSColor", DPI(150), &m_colors.pinNetWebOSColor);

		ImGui::Dummy(ImVec2(1, DPI(10)));
		ImGui::Text("Annotations");
		ImGui::Separator();
		ColorPreferencesItem("Box", DPI(200), "##AnnBox", "annotationBoxColor", DPI(150), &m_colors.annotationBoxColor);
		ColorPreferencesItem("Stalk", DPI(200), "##AnnStalk", "annotationStalkColor", DPI(150), &m_colors.annotationStalkColor);
		ColorPreferencesItem(
		    "Popup text", DPI(200), "##AnnPopupText", "annotationPopupTextColor", DPI(150), &m_colors.annotationPopupTextColor);
		ColorPreferencesItem("Popup background",
		                     DPI(200),
		                     "##AnnPopupBackground",
		                     "annotationPopupBackgroundColor",
		                     DPI(150),
		                     &m_colors.annotationPopupBackgroundColor);

		ImGui::Dummy(ImVec2(1, DPI(10)));
		ImGui::Text("Masks");
		ImGui::Separator();
		ColorPreferencesItem("Pins", DPI(200), "##MaskPins", "selectedMaskPins", DPI(150), &m_colors.selectedMaskPins);
		ColorPreferencesItem("Parts", DPI(200), "##MaskParts", "selectedMaskParts", DPI(150), &m_colors.selectedMaskParts);
		ColorPreferencesItem("Outline", DPI(200), "##MaskOutline", "selectedMaskOutline", DPI(150), &m_colors.selectedMaskOutline);
		ColorPreferencesItem("Boost (Pins)", DPI(200), "##BoostPins", "orMaskPins", DPI(150), &m_colors.orMaskPins);
		ColorPreferencesItem("Boost (Parts)", DPI(200), "##BoostParts", "orMaskParts", DPI(150), &m_colors.orMaskParts);
		ColorPreferencesItem("Boost (Outline)", DPI(200), "##BoostOutline", "orMaskOutline", DPI(150), &m_colors.orMaskOutline);

		ImGui::Separator();

		if (ImGui::Button("Done") || keybindings.isPressed("CloseDialog")) {
			m_tooltips_enabled = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (!dummy) {
		m_tooltips_enabled = true;
	}
	//	ImGui::PopStyleColor();
}

void BoardView::Preferences(void) {
	bool dummy = true;
	ImGuiStyle &style = ImGui::GetStyle();
	//	ImGui::PushStyleColor(ImGuiCol_PopupBg, 0xffe0e0e0);
	ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), 0, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Preferences", &dummy, ImGuiWindowFlags_AlwaysAutoResize)) {
		int t;
		if (m_showPreferences) {
			m_showPreferences  = false;
			m_tooltips_enabled = false;
		}

		t = obvconfig.ParseInt("windowX", 1100);
		RA("Window Width", DPI(200));
		ImGui::SameLine();
		if (ImGui::InputInt("##windowX", &t)) {
			if (t > 400) obvconfig.WriteInt("windowX", t);
		}

		t = obvconfig.ParseInt("windowY", 700);
		RA("Window Height", DPI(200));
		ImGui::SameLine();
		if (ImGui::InputInt("##windowY", &t)) {
			if (t > 320) obvconfig.WriteInt("windowY", t);
		}

		const char *oldFont = obvconfig.ParseStr("fontName", "");
		std::vector<char> newFont(oldFont, oldFont + strlen(oldFont) + 1); // Copy string data + '\0' char
		if (newFont.size() < 256)                                          // reserve space for new font name
			newFont.resize(256, '\0');                                     // Max font name length is 255 characters
		RA("Font name", DPI(200));
		ImGui::SameLine();
		if (ImGui::InputText("##fontName", newFont.data(), newFont.size())) {
			obvconfig.WriteStr("fontName", newFont.data());
		}

		t = obvconfig.ParseInt("fontSize", 20);
		RA("Font size", DPI(200));
		ImGui::SameLine();
		if (ImGui::InputInt("##fontSize", &t)) {
			if (t < 8) {
				t = 8;
			} else if (t > 50) { // 50 is a value that hopefully should not break with too many fonts and 1024x1024 texture limits
				t = 50;
			}
			obvconfig.WriteInt("fontSize", t);
		}

		t = obvconfig.ParseInt("dpi", 100);
		RA("Screen DPI", DPI(200));
		ImGui::SameLine();
		if (ImGui::InputInt("##dpi", &t)) {
			if ((t > 25) && (t < 600)) obvconfig.WriteInt("dpi", t);
		}
		ImGui::PushStyleColor(ImGuiCol_Text, 0xff4040ff);
		ImGui::Text("(Program restart is required to properly apply font and DPI changes)");
		ImGui::PopStyleColor();

		ImGui::Separator();

		RA("Board fill spacing", DPI(200));
		ImGui::SameLine();
		if (ImGui::InputInt("##boardFillSpacing", &boardFillSpacing)) {
			obvconfig.WriteInt("boardFillSpacing", boardFillSpacing);
		}

		t = zoomFactor * 10;
		RA("Zoom step", DPI(200));
		ImGui::SameLine();
		if (ImGui::InputInt("##zoomStep", &t)) {
			obvconfig.WriteFloat("zoomFactor", t / 10);
		}

		RA("Zoom modifier", DPI(200));
		ImGui::SameLine();
		if (ImGui::InputInt("##zoomModifier", &zoomModifier)) {
			obvconfig.WriteInt("zoomModifier", zoomModifier);
		}

		RA("Panning step", DPI(200));
		ImGui::SameLine();
		if (ImGui::InputInt("##panningStep", &panFactor)) {
			obvconfig.WriteInt("panFactor", panFactor);
		}

		RA("Pan modifier", DPI(200));
		ImGui::SameLine();
		if (ImGui::InputInt("##panModifier", &panModifier)) {
			obvconfig.WriteInt("panModifier", panModifier);
		}

		ImGui::Dummy(ImVec2(1, DPI(5)));
		RA("Flip Mode", DPI(200));
		ImGui::SameLine();
		{
			if (ImGui::RadioButton("Viewport", &flipMode, 0)) {
				obvconfig.WriteInt("flipMode", flipMode);
			}
			ImGui::SameLine();
			if (ImGui::RadioButton("Mouse", &flipMode, 1)) {
				obvconfig.WriteInt("flipMode", flipMode);
			}
		}
		ImGui::Dummy(ImVec2(1, DPI(5)));

		RA("Annotation flag size", DPI(200));
		ImGui::SameLine();
		if (ImGui::InputInt("##annotationBoxSize", &annotationBoxSize)) {
			obvconfig.WriteInt("annotationBoxSize", annotationBoxSize);
		}

		RA("Annotation flag offset", DPI(200));
		ImGui::SameLine();
		if (ImGui::InputInt("##annotationBoxOffset", &annotationBoxOffset)) {
			obvconfig.WriteInt("annotationBoxOffset", annotationBoxOffset);
		}
		RA("Net web thickness", DPI(200));
		ImGui::SameLine();
		if (ImGui::InputInt("##netWebThickness", &netWebThickness)) {
			obvconfig.WriteInt("netWebThickness", netWebThickness);
		}
		ImGui::Separator();

		RA("Pin-1/A1 count threshold", DPI(200));
		ImGui::SameLine();
		if (ImGui::InputInt("##pinA1threshold", &pinA1threshold)) {
			if (pinA1threshold < 1) pinA1threshold = 1;
			obvconfig.WriteInt("pinA1threshold", pinA1threshold);
		}

		if (ImGui::Checkbox("Pin select masks", &pinSelectMasks)) {
			obvconfig.WriteBool("pinSelectMasks", pinSelectMasks);
		}

		if (ImGui::Checkbox("Pin Halo", &pinHalo)) {
			obvconfig.WriteBool("pinHalo", pinHalo);
		}
		RA("Halo diameter", DPI(200));
		ImGui::SameLine();
		if (ImGui::InputFloat("##haloDiameter", &pinHaloDiameter)) {
			obvconfig.WriteFloat("pinHaloDiameter", pinHaloDiameter);
		}

		RA("Halo thickness", DPI(200));
		ImGui::SameLine();
		if (ImGui::InputFloat("##haloThickness", &pinHaloThickness)) {
			obvconfig.WriteFloat("pinHaloThickness", pinHaloThickness);
		}

		RA("Info Panel Zoom", DPI(200));
		ImGui::SameLine();
		if (ImGui::InputFloat("##partZoomScaleOutFactor", &partZoomScaleOutFactor)) {
			if (partZoomScaleOutFactor < 1.1) partZoomScaleOutFactor = 1.1;
			obvconfig.WriteFloat("partZoomScaleOutFactor", partZoomScaleOutFactor);
		}

		if (ImGui::Checkbox("Center/Zoom Search Results", &m_centerZoomSearchResults)) {
			obvconfig.WriteBool("centerZoomSearchResults", m_centerZoomSearchResults);
		}

		if (ImGui::Checkbox("Show Info Panel", &showInfoPanel)) {
			obvconfig.WriteBool("showInfoPanel", showInfoPanel);
		}

		if (ImGui::Checkbox("Show net web", &showNetWeb)) {
			obvconfig.WriteBool("showNetWeb", showNetWeb);
		}

		if (ImGui::Checkbox("slowCPU", &slowCPU)) {
			obvconfig.WriteBool("slowCPU", slowCPU);
			style.AntiAliasedLines = !slowCPU;
			style.AntiAliasedFill  = !slowCPU;
		}

		ImGui::SameLine();
		if (ImGui::Checkbox("Show FPS", &showFPS)) {
			obvconfig.WriteBool("showFPS", showFPS);
		}

		if (ImGui::Checkbox("Fill Parts", &fillParts)) {
			obvconfig.WriteBool("fillParts", fillParts);
		}

		ImGui::SameLine();
		if (ImGui::Checkbox("Fill Board", &boardFill)) {
			obvconfig.WriteBool("boardFill", boardFill);
		}

		if (ImGui::Checkbox("Show part name", &showPartName)) {
			obvconfig.WriteBool("showPartName", showPartName);
		}

		ImGui::SameLine();
		if (ImGui::Checkbox("Show pin name", &showPinName)) {
			obvconfig.WriteBool("showPinName", showPinName);
		}

#ifdef _WIN32
		RA("PDF software executable", DPI(200));
		ImGui::SameLine();
		static std::string pdfSoftwarePath = obvconfig.ParseStr("pdfSoftwarePath", "SumatraPDF.exe");;
		if (ImGui::InputText("##pdfSoftwarePath", &pdfSoftwarePath)) {
			obvconfig.WriteStr("pdfSoftwarePath", pdfSoftwarePath.c_str());
		}
		ImGui::SameLine();
		if (ImGui::Button("Browse##pdfSoftwarePath")) {
			auto path = show_file_picker();
			if (!path.empty()) {
				pdfSoftwarePath = path.string();
				obvconfig.WriteStr("pdfSoftwarePath", pdfSoftwarePath.c_str());
			}
		}
#endif

		ImGui::Separator();
		{
			char keybuf[1024];
			int i;
			ImGui::Text("FZ Key");
			ImGui::SameLine();
			for (i = 0; i < 44; i++) {
				snprintf(keybuf + (i * 12), (1023 - (i * 12)),
				        "0x%08lx%s",
				        (long unsigned int)FZKey[i],
				        (i != 43) ? ((i + 1) % 4 ? "  " : "\r\n")
				                  : ""); // yes, a nested inline-if-else. add \r\n after every 4 values, except if the last
			}
			if (ImGui::InputTextMultiline(
			        "##fzkey", keybuf, sizeof(keybuf), ImVec2(DPI(450), ImGui::GetTextLineHeight() * 12.5), 0, NULL, keybuf)) {

				// Strip the line breaks out.
				char *c = keybuf;
				while (*c) {
					if ((*c == '\r') || (*c == '\n')) *c = ' ';
					c++;
				}
				obvconfig.WriteStr("FZKey", keybuf);
				SetFZKey(keybuf);
			}
		}

		ImGui::Separator();

		if (ImGui::Button("Done") || keybindings.isPressed("CloseDialog")) {
			ImGui::CloseCurrentPopup();
			m_tooltips_enabled = true;
		}
		ImGui::EndPopup();
	}

	if (!dummy) {
		m_tooltips_enabled = true;
	}
	//	ImGui::PopStyleColor();
}

void BoardView::HelpAbout(void) {
	bool dummy = true;
	ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), 0, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("About", &dummy, ImGuiWindowFlags_AlwaysAutoResize)) {
		if (m_showHelpAbout) {
			m_showHelpAbout    = false;
			m_tooltips_enabled = false;
		}
		ImGui::Text("%s %s", OBV_NAME, OBV_VERSION);
		ImGui::Text("Build %s %s", OBV_BUILD, __DATE__ " " __TIME__);
		ImGui::Text(OBV_URL);
		if (ImGui::Button("Close") || keybindings.isPressed("CloseDialog")) {
			m_tooltips_enabled = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::Dummy(ImVec2(1, DPI(10)));
		ImGui::Text("License info");
		ImGui::Separator();
		ImGui::TextWrapped(OBV_LICENSE_TEXT);

		ImGui::EndPopup();
	}

	if (!dummy) {
		m_tooltips_enabled = true;
	}
}

void BoardView::HelpControls(void) {
	bool dummy = true;
	ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), 0, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(DPI(540), 0.0f));
	if (ImGui::BeginPopupModal("Controls", &dummy)) {
		if (m_showHelpControls) {
			m_showHelpControls = false;
			m_tooltips_enabled = false;
		}
		ImGui::Text("KEYBOARD CONTROLS");
		ImGui::SameLine();

		if (ImGui::Button("Close") || keybindings.isPressed("CloseDialog")) {
			m_tooltips_enabled = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::Separator();

		ImGui::Columns(2);
		ImGui::PushItemWidth(-1);
		ImGui::Text("Load file");
		ImGui::Text("Quit");
		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::Text("Pan up");
		ImGui::Text("Pan down");
		ImGui::Text("Pan left");
		ImGui::Text("Pan right");
		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::Text("Toggle information panel");
		ImGui::Text("Search for component/Net");
		ImGui::Text("Display component list");
		ImGui::Text("Display net list");
		ImGui::Text("Clear all highlighted items");
		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::Text("Mirror board");
		ImGui::Text("Flip board");
		ImGui::Text(" ");

		ImGui::Text("Zoom in");
		ImGui::Text("Zoom out");
		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::Text("Reset zoom and center");
		ImGui::Text("Rotate clockwise");
		ImGui::Text("Rotate anticlockwise");
		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::Text("Toggle Pin Blanking");

		/*
		 * NEXT COLUMN
		 */
		ImGui::NextColumn();
		ImGui::Text("CTRL-o");
		ImGui::Text("CTRL-q");
		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::Text("w, numpad-up, arrow-up");
		ImGui::Text("s, numpad-down, arrow-down");
		ImGui::Text("a, numpad-left, arrow-left");
		ImGui::Text("d, numpad-right, arrow-right");
		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::Text("i");
		ImGui::Text("CTRL-f, /");
		ImGui::Text("k");
		ImGui::Text("l");
		ImGui::Text("ESC");
		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::Text("m");
		ImGui::Text("Space bar");
		ImGui::Text("(+shift to hold position)");
		ImGui::Spacing();

		ImGui::Text("+, numpad+");
		ImGui::Text("-, numpad-");
		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::Text("x, numpad-5");
		ImGui::Text("'.' numpad '.'");
		ImGui::Text("',' numpad-0");
		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::Text("p");

		ImGui::Columns(1);
		ImGui::Separator();

		ImGui::Text("MOUSE CONTROLS");
		ImGui::Separator();
		ImGui::Columns(2);
		ImGui::Text("Highlight pins on network");
		ImGui::Text("Move board");
		ImGui::Text("Zoom (CTRL for finer steps)");
		ImGui::Text("Flip board");
		ImGui::Text("Annotations menu");

		ImGui::NextColumn();
		ImGui::Text("Click (on pin)");
		ImGui::Text("Click and drag");
		ImGui::Text("Scroll");
		ImGui::Text("Middle click");
		ImGui::Text("Right click");
		ImGui::Columns(1);

		ImGui::EndPopup();
	}
	if (!dummy) {
		m_tooltips_enabled = true;
	}
}

void BoardView::ShowInfoPane(void) {
	ImGuiIO &io = ImGui::GetIO();
	ImVec2 ds   = io.DisplaySize;

	if (!showInfoPanel) return;

	if (m_info_surface.x < DPIF(100)) {
		//	fprintf(stderr,"Too small (%f), set to (%f)\n", width, DPIF(100));
		m_info_surface.x  = DPIF(100) + 1;
		m_board_surface.x = ds.x - m_info_surface.x;
	}

	m_info_surface.y = m_board_surface.y;

	/*
	 * Originally the dialog was to follow the cursor but it proved to be an overkill
	 * to try adjust things to keep it within the bounds of the window so as to not
	 * lose the buttons.
	 *
	 * Now it's kept at a fixed point.
	 */
	ImGui::SetNextWindowPos(ImVec2(ds.x - m_info_surface.x, m_menu_height));
	ImGui::SetNextWindowSize(m_info_surface);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::Begin("Info Panel",
	             NULL,
	             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
	                 ImGuiWindowFlags_NoSavedSettings);

	if ((m_dragging_token == 0) && (io.MousePos.x > m_board_surface.x) && (io.MousePos.x < (m_board_surface.x + DPIF(12.0f)))) {
		ImDrawList *draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(ImVec2(m_board_surface.x, m_menu_height),
		                    ImVec2(m_board_surface.x + DPIF(12.0f), m_board_surface.y + m_menu_height),
		                    ImColor(0x88888888));
		//			DrawHex( draw, io.MousePos, DPIF(10.0f), ImColor(0xFF0000FF) );
	}

	if (ImGui::IsMouseDragging(0)) {
		if ((m_dragging_token == 0) && (io.MouseClickedPos[0].x > m_board_surface.x) &&
		    (io.MouseClickedPos[0].x < (m_board_surface.x + DPIF(20.0f))))
			m_dragging_token = 2; // own it.
		if (m_dragging_token == 2) {
			ImVec2 delta = ImGui::GetMouseDragDelta();
			if ((abs(delta.x) > 500) || (abs(delta.y) > 500)) {
				delta.x = 0;
				delta.y = 0;
			} // If the delta values are crazy just drop them (happens when panning
			// off screen). 500 arbritary chosen
			ImGui::ResetMouseDragDelta();
			m_board_surface.x += delta.x;
			m_info_surface.x = ds.x - m_board_surface.x;
			if (m_board_surface.x < ds.x * 0.66) {
				m_board_surface.x = ds.x * 0.66;
				m_info_surface.x  = ds.x - m_board_surface.x;
			}
			if (delta.x > 0) m_needsRedraw = true;
		}
	} else {
		if (m_dragging_token == 2) {
			obvconfig.WriteInt("infoPanelWidth", m_info_surface.x);
		}
		m_dragging_token = 0;
	}

	if (m_board) {
		ImGui::Columns(2);
		ImGui::Text("Pins: %zu", m_board->Pins().size());
		ImGui::Text("Parts: %zu", m_board->Components().size());
		ImGui::NextColumn();
		ImGui::Text("Nets: %zu", m_board->Nets().size());
		ImGui::TextWrapped("Size: %0.2f x %0.2f\"", m_boardWidth / 1000.0f, m_boardHeight / 1000.0f);
		ImGui::Columns(1);
		ImGui::Separator();
		if (ImGui::Checkbox("Zoom on selected net", &infoPanelCenterZoomNets)) {
			obvconfig.WriteBool("infoPanelCenterZoomNets", infoPanelCenterZoomNets);
		}
		if (ImGui::Checkbox("Select all parts on net", &infoPanelSelectPartsOnNet)) {
			obvconfig.WriteBool("infoPanelSelectPartsOnNet", infoPanelSelectPartsOnNet);
		}
		if (ImGui::Checkbox("Find all parts not ground", &infoPanelSelectPartsOnNetOnlyNotGround)) {

		}
	} else {
		ImGui::Text("No board currently loaded.");
	}

	if (m_partHighlighted.size()) {
		ImGui::Separator();
		ImGui::TextUnformatted((std::to_string(m_partHighlighted.size()) + " parts selected").c_str());

		for (auto part : m_partHighlighted) {

			ImGui::Text(" ");

			if (ImGui::SmallButton(part->name.c_str())) {
				if (!BoardElementIsVisible(part)) FlipBoard();
				if (part->centerpoint.x && part->centerpoint.y) {
					ImVec2 psz;

					/*
					 * Check to see if we need to zoom BACK a bit to fit the part in to view
					 */
					psz = ImVec2(abs(part->outline[2].x - part->outline[0].x) * m_scale / m_board_surface.x,
					             abs(part->outline[2].y - part->outline[0].y) * m_scale / m_board_surface.y);
					if ((psz.x > 1) || (psz.y > 1)) {
						if (psz.x > psz.y) {
							m_scale /= (partZoomScaleOutFactor * psz.x);
						} else {
							m_scale /= (partZoomScaleOutFactor * psz.y);
						}
					}

					SetTarget(part->centerpoint.x, part->centerpoint.y);
				} else {
					SetTarget(part->pins[0]->position.x, part->pins[0]->position.y);
				}
				m_needsRedraw = 1;
			}
			ImGui::SameLine();
			{
				char bn[128];
				snprintf(bn, sizeof(bn), "Z##%s", part->name.c_str());
				if (ImGui::SmallButton(bn)) {
					if (!BoardElementIsVisible(part)) FlipBoard();
					if (part->centerpoint.x && part->centerpoint.y) {
						ImVec2 psz;

						/*
						 * Check to see if we need to zoom BACK a bit to fit the part in to view
						 */
						psz = ImVec2(abs(part->outline[2].x - part->outline[0].x) * m_scale / m_board_surface.x,
						             abs(part->outline[2].y - part->outline[0].y) * m_scale / m_board_surface.y);
						if (psz.x > psz.y) {
							m_scale /= (partZoomScaleOutFactor * psz.x);
						} else {
							m_scale /= (partZoomScaleOutFactor * psz.y);
						}

						SetTarget(part->centerpoint.x, part->centerpoint.y);
					} else {
						SetTarget(part->pins[0]->position.x, part->pins[0]->position.y);
					}
					m_needsRedraw = 1;
				}
			}

			ImGui::SameLine();
			ImGui::Text("%zu Pin(s)", part->pins.size());
			ImGui::SameLine();
			{
				char name_and_id[128];
				snprintf(name_and_id, sizeof(name_and_id), "Copy##%s", part->name.c_str());
				if (ImGui::SmallButton(name_and_id)) {
					// std::string speed is no concern here, since button action is not in UI rendering loop
					std::string to_copy = part->name;
					if (part->mfgcode.size()) {
						to_copy += " " + part->mfgcode;
					}
					for (const auto &pin : part->pins) {
						to_copy += "\n" + pin->name + " " + pin->net->name;
					}
					ImGui::SetClipboardText(to_copy.c_str());
				}
			}

			{
				static bool wholeWordsOnly = true;
				static bool caseSensitive = false;
				std::string pdfButtonName = "PDF Search##" + part->name;
				if (ImGui::SmallButton(pdfButtonName.c_str())) {
					pdfBridge.DocumentSearch(part->name, wholeWordsOnly, caseSensitive);
				}
				ImGui::SameLine();
				ImGui::Checkbox("Whole words only", &wholeWordsOnly);
				ImGui::SameLine();
				ImGui::Checkbox("Case sensitive", &caseSensitive);
			}

			if (part->mfgcode.size()) ImGui::TextWrapped("%s", part->mfgcode.c_str());

			/*
			 * Generate the pin# and net table
			 */
			ImGui::PushItemWidth(-1);
			std::string str = std::string("##") + part->name;
			ImVec2 listSize;
			int pc = part->pins.size();
			if (pc > 20) pc = 20;
			listSize = ImVec2(m_info_surface.x - DPIF(50), pc * ImGui::GetFontSize() * 1.45);
			if (ImGui::BeginListBox(str.c_str(), listSize)) { //, ImVec2(m_board_surface.x/3 -5, m_board_surface.y/2));
				for (auto pin : part->pins) {
					char ss[1024];
					snprintf(ss, sizeof(ss), "%4s  %s", pin->name.c_str(), pin->net->name.c_str());
					if (ImGui::Selectable(ss, (pin == m_pinSelected))) {
						ClearAllHighlights();

						if ((pin->type == Pin::kPinTypeNotConnected) || (pin->type == Pin::kPinTypeUnkown) || (pin->net->is_ground)) {
							m_partHighlighted.push_back(pin->component);
							// do nothing for now
							//
						} else {
							m_pinSelected = pin;
							for (auto p : m_partHighlighted) {
								pin->component->visualmode = pin->component->CVMNormal;
							};
							m_partHighlighted.push_back(pin->component);
							CenterZoomNet(pin->net->name);
						}
						m_needsRedraw = true;
					}
					ImGui::PushStyleColor(ImGuiCol_Border, 0xffeeeeee);
					ImGui::Separator();
					ImGui::PopStyleColor();
				}
				ImGui::EndListBox();
			}
			ImGui::PopItemWidth();

		} // for each part in the list
		ImGui::Text(" ");
		ImGui::Separator();
		ImGui::Separator();
		ImGui::Separator();
	}

	ImGui::End();
	ImGui::PopStyleVar();
}

void BoardView::ContextMenu(void) {
	bool dummy                       = true;
	static char contextbuf[10240]    = "";
	static char contextbufnew[10240] = "";
	static std::string pin, partn, net;
	double tx, ty;

	ImVec2 pos = ScreenToCoord(m_showContextMenuPos.x, m_showContextMenuPos.y);

	/*
	 * So we don't have dozens of near-same-spot annotation points, we truncate
	 * back to integer levels, which still gives us 1-thou precision
	 */
	tx = trunc(pos.x);
	ty = trunc(pos.y);

	/*
	 * Originally the dialog was to follow the cursor but it proved to be an overkill
	 * to try adjust things to keep it within the bounds of the window so as to not
	 * lose the buttons.
	 *
	 * Now it's kept at a fixed point.
	 */
	ImGui::SetNextWindowPos(ImVec2(DPIF(50), DPIF(50)));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (ImGui::BeginPopupModal("Annotations", &dummy, ImGuiWindowFlags_AlwaysAutoResize)) {

		if (m_showContextMenu) {
			contextbuf[0]      = 0;
			m_showContextMenu  = false;
			m_tooltips_enabled = false;
			//			m_parent_occluded = true;
			for (auto &ann : m_annotations.annotations) ann.hovered = false;
		}

		/*
		 * For the new annotation possibility, we need to detect our various
		 * attributes that we can bind to, such as pin, net, part
		 */
		{
			/*
			 * we're going to go through all the possible items we can annotate at this position and offer them
			 */

			float min_dist = m_pinDiameter * 1.0f;

			/*
			 * find the closest pin, starting at no more than
			 * 1 radius away
			 */
			min_dist *= min_dist; // all distance squared
			Pin *selection = nullptr;
			for (auto &pin : m_board->Pins()) {
				if (BoardElementIsVisible(pin->component)) {
					float dx   = pin->position.x - pos.x;
					float dy   = pin->position.y - pos.y;
					float dist = dx * dx + dy * dy;
					if (dist < min_dist) {
						selection = pin.get();
						min_dist  = dist;
					}
				}
			}

			/*
			 * If there was a pin selected, we can extract net/part off it
			 */
			Component* selection_component = nullptr;
			if (selection != nullptr) {
				pin   = selection->name;
				partn = selection->component->name;
				net   = selection->net->name;
			} 

			/*
				* ELSE if we didn't get a pin selected, we can still
				* check for a part.
				*
				* There is a problem where we can be on two parts
				* but haven't decided what to do in such a situation
				*/

			for (auto &part : m_board->Components()) {
				int hit = 0;
				//					auto p_part = part.get();

				if (!BoardElementIsVisible(part)) continue;

				// Work out if the point is inside the hull
				{
					auto poly = part->outline;

					for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
						if (((poly[i].y > pos.y) != (poly[j].y > pos.y)) &&
							(pos.x < (poly[j].x - poly[i].x) * (pos.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x))
							hit = !hit;
					}
				} // hull test
				if (hit) {
					selection_component = part.get();
					if (selection != nullptr) {
						partn = part->name;

						ImGui::SameLine();
					}

				}

			} // for each part

			{

				/*
				 * For existing annotations
				 */
				if (m_annotation_clicked_id >= 0) {
					if (m_annotationedit_retain || (m_annotation_clicked_id >= 0)) {
						Annotation ann = m_annotations.annotations[m_annotation_clicked_id];
						if (!m_annotationedit_retain) {
							snprintf(contextbuf, sizeof(contextbuf), "%s", ann.note.c_str());
							m_annotationedit_retain = true;
							m_annotationnew_retain  = false;
						}

						ImGui::Text("%c(%0.0f,%0.0f) %s, %s%c%s%c",
						            m_current_side == kBoardSideBottom ? 'B' : 'T',
						            tx,
						            ty,
						            ann.net.c_str(),
						            ann.part.c_str(),
						            ann.part.size() && ann.pin.size() ? '[' : ' ',
						            ann.pin.c_str(),
						            ann.part.size() && ann.pin.size() ? ']' : ' ');
						ImGui::InputTextMultiline("##annotationedit",
						                          contextbuf,
						                          sizeof(contextbuf),
						                          ImVec2(DPI(600), ImGui::GetTextLineHeight() * 8),
						                          0,
						                          NULL,
						                          contextbuf);

						if (ImGui::Button("Update##1") || keybindings.isPressed("Validate")) {
							m_annotationedit_retain = false;
							m_annotations.Update(m_annotations.annotations[m_annotation_clicked_id].id, contextbuf);
							m_annotations.GenerateList();
							m_needsRedraw      = true;
							m_tooltips_enabled = true;
							// m_parent_occluded = false;
							ImGui::CloseCurrentPopup();
						}
						ImGui::SameLine();
						if (ImGui::Button("Cancel##1")) {
							ImGui::CloseCurrentPopup();
							m_annotationnew_retain = false;
							m_tooltips_enabled     = true;
							// m_parent_occluded = false;
						}
						ImGui::Separator();
					}
				}

				/*
				 * For generating a new annotation
				 */
				if ((m_annotation_clicked_id < 0) && (m_annotationnew_retain == false)) {
					ImGui::Text("%c(%0.0f,%0.0f) %s %s%c%s%c",
					            m_current_side == kBoardSideBottom ? 'B' : 'T',
					            tx,
					            ty,
					            net.c_str(),
					            partn.c_str(),
					            partn.empty() || pin.empty() ? ' ' : '[',
					            pin.c_str(),
					            partn.empty() || pin.empty() ? ' ' : ']');
				}
				if ((m_annotation_clicked_id < 0) || ImGui::Button("Add New##1") || m_annotationnew_retain) {
					static char diodeNew[128];
					static char voltageNew[128];
					static char ohmNew[128];
					static char ohmBlackNew[128];
					static char partTypeNew[128];
					static PinVoltageFlag voltageFlagNew;
					static bool pinMode;
					static bool inferValueMode;
					static PartAngle partAngleNew;
					auto init_fun = [](Pin* selection) {
						memcpy(diodeNew, selection->diode_value.c_str(), std::min<size_t >(sizeof(diodeNew), selection->diode_value.size()));
						memcpy(voltageNew, selection->voltage_value.c_str(), std::min<size_t>(sizeof(diodeNew), selection->voltage_value.size()));
						memcpy(ohmNew, selection->ohm_value.c_str(), std::min<size_t>(sizeof(ohmNew), selection->ohm_value.size()));
						memcpy(ohmBlackNew, selection->ohm_black_value.c_str(), std::min<size_t>(sizeof(ohmBlackNew), selection->ohm_black_value.size()));
						voltageFlagNew = selection->voltage_flag;
					};
					if (m_annotationnew_retain == false) {
						contextbufnew[0]        = 0;
						m_annotationnew_retain  = true;
						m_annotation_clicked_id = -1;
						m_annotationedit_retain = false;
						memset(diodeNew, 0, sizeof (diodeNew));
						memset(voltageNew, 0, sizeof (voltageNew));
						memset(ohmNew, 0, sizeof (ohmNew));
						memset(ohmBlackNew, 0, sizeof (ohmBlackNew));
						memset(partTypeNew, 0, sizeof (partTypeNew));
						voltageFlagNew = PinVoltageFlag::unknown;
						inferValueMode = false;
						partAngleNew = PartAngle::unknown;
						if (selection_component) {
							memcpy(partTypeNew, selection_component->part_type.c_str(), std::min<size_t>(sizeof(partTypeNew), selection_component->part_type.size()));
							partAngleNew = selection_component->angle;
						}
						if (selection) {
							init_fun(selection);
						}
						pinMode = selection;
					}

					ImGui::Text("Create new annotation for: %c(%0.0f,%0.0f) %s %s%c%s%c",
					            m_current_side == kBoardSideBottom ? 'B' : 'T',
					            tx,
					            ty,
					            net.c_str(),
					            partn.c_str(),
					            partn.empty() || pin.empty() ? ' ' : '[',
					            pin.c_str(),
					            partn.empty() || pin.empty() ? ' ' : ']');
					ImGui::Spacing();
					ImGui::InputTextMultiline("New##annotationnew",
						                          contextbufnew,
						                          sizeof(contextbufnew),
						                          ImVec2(DPI(600), ImGui::GetTextLineHeight() * 2),
						                          0,
						                          NULL,
						                          contextbufnew);
					if (selection_component) {
						ImGui::InputText("partType##partTypeNew",
						                 partTypeNew,
						                 sizeof(partTypeNew));

						ImGui::Text("Part Angle: ");
						ImGui::SameLine();
						ImGui::RadioButton("unknown", (int*)&partAngleNew, (int)PartAngle::unknown);
						ImGui::SameLine();
						ImGui::RadioButton("270", (int*)&partAngleNew, (int)PartAngle::_270);
					}
					if (pinMode) {
						ImGui::InputText("Diode##diodeNew",
						                 diodeNew,
						                 sizeof(diodeNew));

						ImGui::InputText("voltage##voltageNew",
						                 voltageNew,
						                 sizeof(voltageNew));

						ImGui::InputText("Ohm##ohmNew",
						                 ohmNew,
						                 sizeof(ohmNew));

						ImGui::InputText("ohmBlack##ohmBlackNew",
						                 ohmBlackNew,
						                 sizeof(ohmBlackNew));
						ImGui::Text("Voltage Flag: ");
						ImGui::SameLine();
						ImGui::RadioButton("unknown", (int*)&voltageFlagNew, (int)PinVoltageFlag::unknown);
						ImGui::SameLine();
						ImGui::RadioButton("Input", (int*)&voltageFlagNew, (int)PinVoltageFlag::input);
						ImGui::SameLine();
						ImGui::RadioButton("Output", (int*)&voltageFlagNew, (int)PinVoltageFlag::output);

					}


					if (ImGui::Button("Apply##1") || keybindings.isPressed("Validate")) {
						m_tooltips_enabled     = true;
						m_annotationnew_retain = false;
						if (debug) {
							fprintf(stderr, "DATA:'%s'\n\n", contextbufnew);
						}
						if (pinMode) {
							auto& pinInfo = m_annotations.NewPinInfo(selection->component->name.c_str(), selection->name.c_str());
							pinInfo.diode = selection->diode_value = diodeNew;
							pinInfo.voltage = selection->voltage_value = voltageNew;
							pinInfo.ohm = selection->ohm_value = ohmNew;
							pinInfo.ohm_black = selection->ohm_black_value = ohmBlackNew;					
							pinInfo.voltage_flag = selection->voltage_flag = voltageFlagNew;					
						}
						if (selection_component) {
							auto& partInfo = m_annotations.NewPartInfo(selection_component->name.c_str());;
							partInfo.part_type = partTypeNew;
							selection_component->set_part_type(partTypeNew);
							if (partAngleNew != selection_component->angle) {
								partInfo.angle = selection_component->angle = partAngleNew;
							}
						}
						m_annotations.SavePinInfos();

						if (!std::string_view {contextbufnew}.empty()) {
							m_annotations.Add(m_current_side == kBoardSideBottom, tx, ty, net.c_str(), partn.c_str(), pin.c_str(), contextbufnew);
							m_annotations.GenerateList();
						}
						m_needsRedraw = true;

						ImGui::CloseCurrentPopup();
					}
					/*
					ImGui::SameLine();
					if (ImGui::Button("Cancel")) {
					    ImGui::CloseCurrentPopup();
					    m_tooltips_enabled     = true;
					    m_annotationnew_retain = false;
					}
					*/
				} else {
					ImGui::SameLine();
				}

				if ((m_annotation_clicked_id >= 0) && (ImGui::Button("Remove"))) {
					m_annotations.Remove(m_annotations.annotations[m_annotation_clicked_id].id);
					m_annotations.GenerateList();
					m_needsRedraw = true;
					// m_parent_occluded = false;
					ImGui::CloseCurrentPopup();
				}
			}

			// the position.
		}

		ImGui::SameLine();
		if (ImGui::Button("Cancel##2") || keybindings.isPressed("CloseDialog")) {
			m_annotationnew_retain  = false;
			m_annotationedit_retain = false;
			m_tooltips_enabled      = true;
			// m_parent_occluded = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
		//	m_tooltips_enabled = true;
	}
	ImGui::PopStyleVar();

	/** if the dialog was closed by using the (X) icon **/
	if (!dummy) {
		m_annotationnew_retain  = false;
		m_annotationedit_retain = false;
		m_tooltips_enabled      = true;
		// m_parent_occluded = false;
	}
}

std::pair<SharedVector<Component>, SharedVector<Net>> BoardView::SearchPartsAndNets(const char *search, int limit) {
	SharedVector<Component> parts;
	SharedVector<Net> nets;
	if (m_searchComponents) parts = searcher.parts(search, limit);
	if (m_searchNets) nets = searcher.nets(search, limit);
	return {parts, nets};
}

const char *getcname(const std::string &name) {
	return name.c_str();
}

template <class T>
const char *getcname(const T &t) {
	return t->name.c_str();
}

template <class T>
void BoardView::ShowSearchResults(std::vector<T> results, char *search, int &limit, void (BoardView::*onSelect)(const char *)) {
	for (auto &r : results) {
		const char *cname = getcname(r);
		if (ImGui::Selectable(cname, false)) {
			(this->*onSelect)(cname);
			snprintf(search, 128, "%s", cname);
			limit = 0;
		}
		limit--;
	}
}

void BoardView::SearchColumnGenerate(const std::string &title,
                                     std::pair<SharedVector<Component>, SharedVector<Net>> results,
                                     char *search,
                                     int limit) {
	if (ImGui::BeginListBox(title.c_str())) {

		if (m_searchComponents) {
			if (results.first.empty() && (!m_searchNets || results.second.empty())) { // show suggestions only if there is no result at all
				auto s = scparts.suggest(search);
				if (s.size() > 0) {
					ImGui::Text("Did you mean...");
					ShowSearchResults(s, search, limit, &BoardView::FindComponent);
				}
			} else
				ShowSearchResults(results.first, search, limit, &BoardView::FindComponent);
		}

		if (m_searchNets) {
			if (results.second.empty() && (!m_searchComponents || results.first.empty())) {
				auto s = scnets.suggest(search);
				if (s.size() > 0) {
					ImGui::Text("Did you mean...");
					ShowSearchResults(s, search, limit, &BoardView::FindNet);
				}
			} else
				ShowSearchResults(results.second, search, limit, &BoardView::FindNet);
		}

		ImGui::EndListBox();
	}
}

void BoardView::SearchComponent(void) {
	bool dummy = true;

	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x/2, DPI(100)), 0, ImVec2(0.5f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (ImGui::BeginPopupModal("Search for Component / Network",
	                           &dummy,
	                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		//		char cs[128];
		const char *first_button[] = {m_search[0], m_search[1], m_search[2]};

		bool search_params_changed = m_showSearch; //treat initiail popup reopening similar to later search option changes
		if (m_showSearch) {
			m_showSearch       = false;
			m_tooltips_enabled = false;
			//			fprintf(stderr, "Tooltips disabled\n");
		}

		// Column 1, implied.
		//
		//
		if (ImGui::Button("Search")) {
			// FindComponent(first_button);
			m_tooltips_enabled = true;
			SearchCompound(first_button[0]);
			SearchCompoundNoClear(first_button[1]);
			SearchCompoundNoClear(first_button[2]);
			CenterZoomSearchResults();
			ImGui::CloseCurrentPopup();
		} // search button

		ImGui::SameLine();
		if (ImGui::Button("Reset")) {
			FindComponent("");
			ResetSearch();
		} // reset button

		ImGui::SameLine();
		if (ImGui::Button("Exit") || keybindings.isPressed("CloseDialog")) {
			FindComponent("");
			ResetSearch();
			m_tooltips_enabled = true;
			ImGui::CloseCurrentPopup();
		} // exit button

		ImGui::SameLine();
		//		ImGui::Dummy(ImVec2(DPI(200), 1));
		//		ImGui::SameLine();
		ImGui::PushItemWidth(-1);
		ImGui::Text("ENTER: Search, ESC: Exit, TAB: next field");

		{
			ImGui::Separator();
			search_params_changed |= ImGui::Checkbox("Components", &m_searchComponents);

			ImGui::SameLine();
			search_params_changed |= ImGui::Checkbox("Nets", &m_searchNets);

			ImGui::SameLine();
			search_params_changed |= ImGui::Checkbox("Including details", &searcher.configSearchDetails());

			ImGui::Text(" Search mode: ");
			ImGui::SameLine();
			if (ImGui::RadioButton("Substring", searcher.isMode(SearchMode::Sub))) {
				search_params_changed = true;
				searcher.setMode(SearchMode::Sub);
			}
			ImGui::SameLine();
			if (ImGui::RadioButton("Prefix", searcher.isMode(SearchMode::Prefix))) {
				search_params_changed = true;
				searcher.setMode(SearchMode::Prefix);
			}
			ImGui::SameLine();
			ImGui::PushItemWidth(-1);
			if (ImGui::RadioButton("Whole", searcher.isMode(SearchMode::Whole))) {
				search_params_changed = true;
				searcher.setMode(SearchMode::Whole);
			}
			ImGui::PopItemWidth();
		}

		ImGui::Separator();

		ImGui::Columns(3);

		for (int i = 0; i < 3; i++) {
			std::string ui_number   = std::to_string(i + 1); // visual UI number is 1-based unlike 0-based indexing
			std::string title       = "Item #" + ui_number;
			std::string searchLabel = "##search" + ui_number;
			ImGui::Text("%s", title.c_str());

			ImGui::PushItemWidth(-1);

			bool textNonEmpty = m_search[i][0] != '\0';                            // Text typed in the search box
			auto results      = SearchPartsAndNets(m_search[i], 30);               // Perform the search for both nets and parts
			bool hasResults   = !results.first.empty() || !results.second.empty(); // We found some nets or some parts

			if (textNonEmpty && !hasResults) ImGui::PushStyleColor(ImGuiCol_FrameBg, 0xFF6666FF);
			bool textChanged =
			    ImGui::InputText(searchLabel.c_str(),
			                     m_search[i],
			                     128,
			                     ImGuiInputTextFlags_CharsNoBlank | (m_search[0] ? ImGuiInputTextFlags_AutoSelectAll : 0));
			if (textNonEmpty && !hasResults) ImGui::PopStyleColor();
			if (ImGui::IsItemActivated()) {
				// user activates another column
				m_active_search_column = i;
				search_params_changed  = true;
			}

			bool this_column_active = i == m_active_search_column;

			if (textChanged || (search_params_changed && this_column_active)) SearchCompound(m_search[i]);

			ImGui::PopItemWidth();

			if (!ImGui::IsItemDeactivated() && this_column_active &&
			    ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() &&
			    !ImGui::IsMouseClicked(0)) {
				ImGui::SetKeyboardFocusHere(-1);
			} // set keyboard focus back to active colun input-text after other type of element was clicked

			ImGui::PushItemWidth(-1);
			if (textNonEmpty) SearchColumnGenerate("##SC" + ui_number, results, m_search[i], 30);
			ImGui::PopItemWidth();
			if (i == 1)
				ImGui::PushItemWidth(DPI(500));
			else if (i == 2)
				ImGui::PopItemWidth();

			ImGui::NextColumn();
		}

		ImGui::PopItemWidth();

		ImGui::Columns(1); // reset back to single column mode
		ImGui::Separator();

		// Enter and Esc close the search:
		if (keybindings.isPressed("Accept")) {
			// SearchCompound(first_button);
			// SearchCompoundNoClear(first_button2);
			// SearchCompoundNoClear(first_button3);
			SearchCompound(m_search[0]);
			SearchCompoundNoClear(m_search[1]);
			SearchCompoundNoClear(m_search[2]);
			CenterZoomSearchResults();
			ImGui::CloseCurrentPopup();
			m_tooltips_enabled = true;
		} // response to keyboard ENTER

		ImGui::EndPopup();
	}
	ImGui::PopStyleVar();
	if (!dummy) {
		m_tooltips_enabled = true;
	}
}

void BoardView::ResetSearch() {
	for (int i = 0; i < 3; i++) m_search[i][0] = '\0';
	m_active_search_column = 0;
}

void BoardView::ClearAllHighlights(void) {
	m_pinSelected = nullptr;
	FindNet("");
	FindComponent("");
	ResetSearch();
	m_needsRedraw      = true;
	m_tooltips_enabled = true;
	if (m_board != NULL) {
		for (auto part : m_board->Components()) part->visualmode = part->CVMNormal;
	}
	m_partHighlighted.clear();
	m_pinHighlighted.clear();
}

/** UPDATE Logic region
 *
 *
 *
 *
 */
void BoardView::Update() {
	bool open_file = false;
	// ImGuiIO &io = ImGui::GetIO();
	char *preset_filename = NULL;
	ImGuiIO &io           = ImGui::GetIO();

	// Window is probably minimized, do not attempt to draw anything as our code will not handle screen size of 0 properly and crash (ocornut/imgui@bb2529d)
	if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) {
		return;
	}

	/**
	 * ** FIXME
	 * This should be handled in the keyboard section, not here
	 */
	if (keybindings.isPressed("Open")) {
		open_file = true;
		// the dialog will likely eat our WM_KEYUP message for CTRL and O:
		io.AddKeyEvent(ImGuiKey_RightCtrl, false);
		io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
		io.AddKeyEvent(ImGuiKey_O, false);

	}

	if (keybindings.isPressed("Quit")) {
		m_wantsQuit = true;
	}

	if (ImGui::BeginMainMenuBar()) {
		m_menu_height = ImGui::GetWindowHeight();

		/*
		 * Create these dialogs, but they're not actually
		 * visible until they're called up by the respective
		 * flags.
		 */
		SearchComponent();
		HelpControls();
		HelpAbout();
		ColorPreferences();
		Preferences();
		ContextMenu();

		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Open", keybindings.getKeyNames("Open").c_str())) {
				open_file = true;
			}

			/// Generate file history - PLD20160618-2028
			ImGui::Separator();
			{
				int i;
				for (i = 0; i < fhistory.count; i++) {
					if (ImGui::MenuItem(fhistory.Trim_filename(fhistory.history[i], 2))) {
						open_file       = true;
						preset_filename = fhistory.history[i];
					}
				}
			}
			ImGui::Separator();

			if (ImGui::MenuItem("Part/Net Search", keybindings.getKeyNames("Search").c_str())) {
				if (m_validBoard) m_showSearch = true;
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Program Preferences")) {
				m_showPreferences = true;
			}

			if (ImGui::MenuItem("Colour Preferences")) {
				m_showColorPreferences = true;
			}

			keyboardPreferences.menuItem();
			boardSettings.menuItem();

			ImGui::Separator();

			if (ImGui::MenuItem("Quit", keybindings.getKeyNames("Quit").c_str())) {
				m_wantsQuit = true;
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View")) {
			if (ImGui::MenuItem("Flip board", keybindings.getKeyNames("Flip").c_str())) {
				FlipBoard();
			}
			if (ImGui::MenuItem("Rotate CW", keybindings.getKeyNames("RotateCW").c_str())) {
				Rotate(1);
			}
			if (ImGui::MenuItem("Rotate CCW", keybindings.getKeyNames("RotateCCW").c_str())) {
				Rotate(-1);
			}
			if (ImGui::MenuItem("Reset View", keybindings.getKeyNames("Center").c_str())) {
				CenterView();
			}
			if (ImGui::MenuItem("Mirror Board", keybindings.getKeyNames("Mirror").c_str())) {
				Mirror();
			}

			if (ImGui::MenuItem("Toggle Pin Display", keybindings.getKeyNames("TogglePins").c_str())) {
				showPins ^= 1;
				m_needsRedraw = true;
			}

			if (ImGui::MenuItem("Show Info Panel", keybindings.getKeyNames("InfoPanel").c_str())) {
				showInfoPanel ^= 1;
				obvconfig.WriteBool("showInfoPanel", showInfoPanel ? true : false);
				m_needsRedraw = true;
			}

			ImGui::Separator();
			if (ImGui::Checkbox("Show FPS", &showFPS)) {
				obvconfig.WriteBool("showFPS", showFPS);
				m_needsRedraw = true;
			}

			if (ImGui::Checkbox("Show Position", &showPosition)) {
				obvconfig.WriteBool("showPosition", showPosition);
				m_needsRedraw = true;
			}

			if (ImGui::Checkbox("Net web", &showNetWeb)) {
				obvconfig.WriteBool("showNetWeb", showNetWeb);
				m_needsRedraw = true;
			}

			if (ImGui::Checkbox("Annotations", &showAnnotations)) {
				obvconfig.WriteBool("showAnnotations", showAnnotations);
				m_needsRedraw = true;
			}

			if (ImGui::Checkbox("Board fill", &boardFill)) {
				obvconfig.WriteBool("boardFill", boardFill);
				m_needsRedraw = true;
			}

			if (ImGui::Checkbox("Part fill", &fillParts)) {
				obvconfig.WriteBool("fillParts", fillParts);
				m_needsRedraw = true;
			}

			if (ImGui::Checkbox("Part name", &showPartName)) {
				obvconfig.WriteBool("showPartName", showPartName);
				m_needsRedraw = true;
			}

			if (ImGui::Checkbox("Pin name", &showPinName)) {
				obvconfig.WriteBool("showPinName", showPinName);
				m_needsRedraw = true;
			}

			if (ImGui::Checkbox("Background image", &backgroundImage.enabled)) {
				obvconfig.WriteBool("showBackgroundImage", backgroundImage.enabled);
				m_needsRedraw = true;
			}

			if (ImGui::Checkbox("Part type", &showPartType)) {
				obvconfig.WriteBool("showPartType", showPartType);
				m_needsRedraw = true;
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Info Panel", keybindings.getKeyNames("InfoPanel").c_str())) {
				showInfoPanel = !showInfoPanel;
			}
			if (ImGui::MenuItem("Net List", keybindings.getKeyNames("NetList").c_str())) {
				m_showNetList = !m_showNetList;
			}
			if (ImGui::MenuItem("Part List", keybindings.getKeyNames("PartList").c_str())) {
				m_showPartList = !m_showPartList;
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help##1")) {
			if (ImGui::MenuItem("Controls")) {
				m_showHelpControls = true;
			}
			if (ImGui::MenuItem("About")) {
				m_showHelpAbout = true;
			}
			ImGui::EndMenu();
		}

		ImGui::SameLine();
		ImGui::Dummy(ImVec2(DPI(10), 1));
		ImGui::SameLine();
		if (ImGui::Checkbox("Annotations", &showAnnotations)) {
			obvconfig.WriteBool("showAnnotations", showAnnotations);
			m_needsRedraw = true;
		}

		ImGui::SameLine();
		if (ImGui::Checkbox("Netweb", &showNetWeb)) {
			obvconfig.WriteBool("showNetWeb", showNetWeb);
			m_needsRedraw = true;
		}

		ImGui::SameLine();
		{
			if (ImGui::Checkbox("Pins", &showPins)) {
				obvconfig.WriteBool("showPins", showPins);
				m_needsRedraw = true;
			}
		}

		ImGui::SameLine();
		if (ImGui::Checkbox("Image", &backgroundImage.enabled)) {
			obvconfig.WriteBool("showBackgroundImage", backgroundImage.enabled);
			m_needsRedraw = true;
		}

		ImGui::SameLine();
		ImGui::Dummy(ImVec2(DPI(40), 1));

		ImGui::SameLine();
		if (ImGui::Button(" - ")) {
			Zoom(m_board_surface.x / 2, m_board_surface.y / 2, -zoomFactor);
		}
		ImGui::SameLine();
		if (ImGui::Button(" + ")) {
			Zoom(m_board_surface.x / 2, m_board_surface.y / 2, zoomFactor);
		}
		ImGui::SameLine();
		ImGui::Dummy(ImVec2(DPI(20), 1));
		ImGui::SameLine();
		if (ImGui::Button("-")) {
			Zoom(m_board_surface.x / 2, m_board_surface.y / 2, -zoomFactor / zoomModifier);
		}
		ImGui::SameLine();
		if (ImGui::Button("+")) {
			Zoom(m_board_surface.x / 2, m_board_surface.y / 2, zoomFactor / zoomModifier);
		}

		ImGui::SameLine();
		ImGui::Dummy(ImVec2(DPI(20), 1));
		ImGui::SameLine();
		if (ImGui::Button(" < ")) {
			Rotate(-1);
		}

		ImGui::SameLine();
		if (ImGui::Button(" ^ ")) {
			FlipBoard();
		}

		ImGui::SameLine();
		if (ImGui::Button(" > ")) {
			Rotate(1);
		}

		ImGui::SameLine();
		if (ImGui::Button("X")) {
			CenterView();
		}

		ImGui::SameLine();
		if (ImGui::Button("CLEAR")) {
			ClearAllHighlights();
		}

		ImGui::Text(" Show mode:");
		ImGui::SameLine();
		if (ImGui::RadioButton("None", (int*)&showMode, ShowMode_None)) {
			obvconfig.WriteInt("showMode", showMode);
			m_needsRedraw = true;
		}
		if (ImGui::RadioButton("diode", (int*)&showMode, ShowMode_Diode)) {
			obvconfig.WriteInt("showMode", showMode);
			m_needsRedraw = true;
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("voltage", (int*)&showMode, ShowMode_Voltage)) {
			obvconfig.WriteInt("showMode", showMode);
			m_needsRedraw = true;
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("ohm", (int*)&showMode, ShowMode_Ohm)) {
			obvconfig.WriteInt("showMode", showMode);
			m_needsRedraw = true;
		}
		

		/*
		ImGui::SameLine();
		ImVec2 tz = ImGui::CalcTextSize("Search             ");
		ImGui::PushItemWidth(-tz.x);
		ImGui::Text("Search");
		ImGui::SameLine();
		ImGui::PushItemWidth(-DPI(1));
		if (ImGui::InputText("##ansearch", m_search, 128, 0)) {
		}
		ImGui::PopItemWidth();
		*/

		if (m_showContextMenu && m_file && showAnnotations) {
			ImGui::OpenPopup("Annotations");
		}

		if (m_showHelpAbout) {
			ImGui::OpenPopup("About");
		}
		if (m_showHelpControls) {
			ImGui::OpenPopup("Controls");
		}

		if (m_showColorPreferences) {
			ImGui::OpenPopup("Colour Preferences");
		}

		if (m_showPreferences) {
			ImGui::OpenPopup("Preferences");
		}

		keyboardPreferences.render();
		boardSettings.render();

		if (m_showSearch && m_file) {
			ImGui::OpenPopup("Search for Component / Network");
		}

		if (ImGui::BeginPopupModal("Error opening file")) {
			ImGui::Text("There was an error opening the file: %s", m_lastFileOpenName.c_str());
			if (!m_error_msg.empty()) {
				ImGui::Text("%s", m_error_msg.c_str());
			}
			if (m_file && !m_file->error_msg.empty()) {
				ImGui::Text("%s", m_file->error_msg.c_str());
			}
			if (ImGui::Button("OK")) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		if (m_lastFileOpenWasInvalid) {
			ImGui::OpenPopup("Error opening file");
			m_lastFileOpenWasInvalid = false;
		}
		ImGui::EndMainMenuBar();
	}

	if (open_file) {
		filesystem::path filepath;

		if (preset_filename) {
			filepath        = filesystem::u8path(preset_filename);
			preset_filename = NULL;
		} else {
			filepath = show_file_picker(true);

			ImGuiIO &io           = ImGui::GetIO();
			io.MouseDown[0]       = false;
			io.MouseClicked[0]    = false;
			io.MouseClickedPos[0] = ImVec2(0, 0);
		}

		if (!filepath.empty()) {
			LoadFile(filepath);
		}
	}

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
	                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

	ImGuiWindowFlags draw_surface_flags = flags | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse;

	/*
	 * Status footer
	 */
	m_status_height = (DPIF(10.0f) + ImGui::GetFontSize());
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(DPIF(4.0f), DPIF(3.0f)));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y - m_status_height));
	ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, m_status_height));
	ImGui::Begin("status", nullptr, flags | ImGuiWindowFlags_NoFocusOnAppearing);
	if (m_file && m_board && m_pinSelected) {
		auto pin = m_pinSelected;
		ImGui::Text("Part: %s   Pin: %s   Net: %s   Probe: %d   (%s.) Voltage: %s  Ohm: %s OhmBlack: %s",
		            pin->component->name.c_str(),
		            pin->name.c_str(),
		            pin->net->name.c_str(),
		            pin->net->number,
		            pin->component->mount_type_str().c_str(),
		            pin->voltage_value.c_str(),
		            pin->ohm_value.c_str(),
		            pin->ohm_black_value.c_str()
					);
	} else {
		ImVec2 spos = ImGui::GetMousePos();
		ImVec2 pos  = ScreenToCoord(spos.x, spos.y);
		if (showFPS == true) {
			ImGui::Text("FPS: %0.0f ", ImGui::GetIO().Framerate);
			ImGui::SameLine();
		}

		if (debug) {
			ImGui::Text("AnnID:%d ", m_annotation_clicked_id);
			ImGui::SameLine();
		}

		if (showPosition == true) {
			ImGui::Text("Position: %0.3f\", %0.3f\" (%0.2f, %0.2fmm)", pos.x / 1000, pos.y / 1000, pos.x * 0.0254, pos.y * 0.0254);
			ImGui::SameLine();
		}

		{
			if (m_validBoard) {
				ImVec2 s = ImGui::CalcTextSize(fhistory.history[0]);
				ImGui::SameLine(ImGui::GetWindowWidth() - s.x - 20);
				ImGui::Text("%s", fhistory.history[0]);
				ImGui::SameLine();
			}
		}
		ImGui::Text(" ");
	}
	ImGui::End();
	ImGui::PopStyleVar();
	ImGui::PopStyleVar();
	ImGui::PopStyleVar();

	if (!showInfoPanel) {
		m_board_surface = ImVec2(io.DisplaySize.x, io.DisplaySize.y - (m_status_height + m_menu_height));
	} else {
		m_board_surface = ImVec2(io.DisplaySize.x - m_info_surface.x, io.DisplaySize.y - (m_status_height + m_menu_height));
	}
	/*
	 * Drawing surface, where the actual PCB/board is plotted out
	 */
	ImGui::SetNextWindowPos(ImVec2(0, m_menu_height));
	if (io.DisplaySize.x != m_lastWidth || io.DisplaySize.y != m_lastHeight) {
		//		m_lastWidth   = io.DisplaySize.x;
		//		m_lastHeight  = io.DisplaySize.y;
		m_lastWidth   = m_board_surface.x;
		m_lastHeight  = m_board_surface.y;
		m_needsRedraw = true;
	}

	ImGui::SetNextWindowSize(m_board_surface);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, m_colors.backgroundColor);

	ImGui::Begin("surface", nullptr, draw_surface_flags);
	if (m_validBoard) {
		HandleInput();
		backgroundImage.render(*ImGui::GetWindowDrawList(),
			CoordToScreen(backgroundImage.x0(), backgroundImage.y0()),
			CoordToScreen(backgroundImage.x1(), backgroundImage.y1()),
			m_rotation);
		DrawBoard();
	}
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();

	// Overlay
	RenderOverlay();

	ImGui::PopStyleVar();

	HandlePDFBridgeSelection();

} // main menu bar

void BoardView::Zoom(float osd_x, float osd_y, float zoom) {
	ImVec2 target;
	ImVec2 coord;
	ImGuiIO &io = ImGui::GetIO();

	if (io.KeyCtrl) zoom /= zoomModifier;

	target.x = osd_x;
	target.y = osd_y;
	coord    = ScreenToCoord(target.x, target.y);

	// Adjust the scale of the whole view, then get the new coordinates ( as
	// CoordToScreen utilises m_scale )
	m_scale        = m_scale * powf(2.0f, zoom);
	ImVec2 dtarget = CoordToScreen(coord.x, coord.y);

	ImVec2 td = ScreenToCoord(target.x - dtarget.x, target.y - dtarget.y, 0);
	m_dx += td.x;
	m_dy += td.y;
	m_needsRedraw = true;
}

void BoardView::Pan(int direction, int amount) {
	ImGuiIO &io = ImGui::GetIO();
#define DIR_UP 1
#define DIR_DOWN 2
#define DIR_LEFT 3
#define DIR_RIGHT 4

	amount = amount / m_scale;

	if (io.KeyCtrl) amount /= panModifier;

	switch (direction) {
		case DIR_UP: amount = -amount;
		case DIR_DOWN:
			if ((m_current_side == kBoardSideBottom ) && (m_rotation % 2)) amount = -amount;
			switch (m_rotation) {
				case 0: m_dy += amount; break;
				case 1: m_dx -= amount; break;
				case 2: m_dy -= amount; break;
				case 3: m_dx += amount; break;
			}
			break;
		case DIR_LEFT: amount = -amount;
		case DIR_RIGHT:
			if ((m_current_side == kBoardSideBottom) && ((m_rotation % 2) == 0)) amount = -amount;
			switch (m_rotation) {
				case 0: m_dx -= amount; break;
				case 1: m_dy -= amount; break;
				case 2: m_dx += amount; break;
				case 3: m_dy += amount; break;
			}
			break;
	}

	m_draggingLastFrame = true;
	m_needsRedraw       = true;
}

/*
 * This HandleInput() function isn't actually a generic input handler
 * it's meant specifically for the board draw surface only.  The inputs
 * for menus is handled within the menu generation itself.
 */
void BoardView::HandleInput() {
	if (!m_board || (!m_file)) return;

	const ImGuiIO &io = ImGui::GetIO();

	if (ImGui::IsWindowHovered()) {

		if (ImGui::IsMouseDragging(0)) {
			if ((m_dragging_token == 0) && (io.MouseClickedPos[0].x < m_board_surface.x)) m_dragging_token = 1; // own it.
			if (m_dragging_token == 1) {
				//		   if ((io.MouseClickedPos[0].x < m_info_surface.x)) {
				ImVec2 delta = ImGui::GetMouseDragDelta();
				if ((abs(delta.x) > 500) || (abs(delta.y) > 500)) {
					delta.x = 0;
					delta.y = 0;
				} // If the delta values are crazy just drop them (happens when panning
				// off screen). 500 arbritary chosen
				ImGui::ResetMouseDragDelta();
				ImVec2 td = ScreenToCoord(delta.x, delta.y, 0);
				m_dx += td.x;
				m_dy += td.y;
				m_draggingLastFrame = true;
				m_needsRedraw       = true;
			}
		} else if (m_dragging_token >= 0) {
			m_dragging_token = 0;

			if (m_lastFileOpenWasInvalid == false) {
				// Conext menu
				if (!m_lastFileOpenWasInvalid && m_file && m_board && ImGui::IsMouseClicked(1)) {
					if (showAnnotations) {
						// Build context menu here, for annotations and inspection
						//
						ImVec2 spos = ImGui::GetMousePos();
						if (AnnotationIsHovered()) m_annotation_clicked_id = m_annotation_last_hovered;

						m_annotationedit_retain = false;
						m_showContextMenu       = true;
						m_showContextMenuPos    = spos;
						m_tooltips_enabled      = false;
						m_needsRedraw           = true;
						if (debug) fprintf(stderr, "context click request at (%f %f)\n", spos.x, spos.y);
					}

					// Flip the board with the middle click
				} else if (!m_lastFileOpenWasInvalid && m_file && m_board && ImGui::IsMouseReleased(2)) {
					FlipBoard();

					// Else, click to select pin
				} else if (!m_lastFileOpenWasInvalid && m_file && m_board && ImGui::IsMouseReleased(0) && !m_draggingLastFrame) {
					ImVec2 spos = ImGui::GetMousePos();
					ImVec2 pos  = ScreenToCoord(spos.x, spos.y);

					m_needsRedraw = true;

					// threshold to within a pin's diameter of the pin center
					// float min_dist = m_pinDiameter * 1.0f;
					float min_dist = m_pinDiameter / 2.0f;
					min_dist *= min_dist; // all distance squared
					std::shared_ptr<Pin> selection = nullptr;
					for (auto &pin : m_board->Pins()) {
						if (!pin->net->is_ground && BoardElementIsVisible(pin)) {
							float dx   = pin->position.x - pos.x;
							float dy   = pin->position.y - pos.y;
							float dist = dx * dx + dy * dy;
							if ((dist < std::max(pin->diameter * pin->diameter, min_dist))) {
								selection = pin;
								min_dist  = dist;
							}
						}
					}

					m_pinSelected = selection;
					if (m_pinSelected) {
						if (!io.KeyCtrl) {
							for (auto p : m_board->Components()) {
								p->visualmode = p->CVMNormal;
							}
							m_partHighlighted.resize(0);
							m_pinHighlighted.resize(0);
						}
						m_pinSelected->component->visualmode = m_pinSelected->component->CVMSelected;
						m_partHighlighted.push_back(m_pinSelected->component);
					}

					m_viaSelected = nullptr;
					if (m_pinSelected == nullptr) {
						for (auto &via : m_board->Vias()) {
							if (BoardElementIsVisible(via)) {
								float dx   = via->position.x - pos.x;
								float dy   = via->position.y - pos.y;
								float dist = dx * dx + dy * dy;
								if ((dist < (via->size * via->size)) && (dist < min_dist)) {
									m_viaSelected = via;
									min_dist  = dist;
								}
							}
						}
					}

					if (m_viaSelected) {
						for (auto& pin : m_viaSelected->net->pins) m_pinHighlighted.push_back(pin);
					}

					if (m_pinSelected == nullptr && m_viaSelected == nullptr) {
						bool any_hits = false;

						for (auto &part : m_board->Components()) {
							int hit = 0;
							//							auto p_part = part.get();

							if (!BoardElementIsVisible(part)) continue;
							if (part->component_type == Component::kComponentTypeBoard) continue;

							// Work out if the point is inside the hull
							{
								auto poly = part->outline;

								for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
									if (((poly[i].y > pos.y) != (poly[j].y > pos.y)) &&
									    (pos.x <
									     (poly[j].x - poly[i].x) * (pos.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x))
										hit ^= 1;
								}
							}

							if (hit) {
								any_hits = true;

								bool partInList = contains(part, m_partHighlighted);

								/*
								 * If the CTRL key isn't held down, then we have to
								 * flush any existing highlighted parts
								 */
								if (io.KeyCtrl) {
									if (!partInList) {
										m_partHighlighted.push_back(part);
										part->visualmode = part->CVMSelected;
									} else {
										remove(part, m_partHighlighted);
										part->visualmode = part->CVMNormal;
									}

								} else {
									for (auto p : m_board->Components()) {
										p->visualmode = p->CVMNormal;
									}
									m_partHighlighted.resize(0);
									m_pinHighlighted.resize(0);
									if (!partInList) {
										m_partHighlighted.push_back(part);
										part->visualmode = part->CVMSelected;
									}
								}

								/*
								 * If this part has a non-selected visual mode (normal)
								 * AND it's not in the existing part list, then add it
								 */
								/*
								if (part->visualmode == part->CVMNormal) {
								    if (!partInList) {
								        m_partHighlighted.push_back(p_part);
								    }
								}

								part->visualmode++;
								part->visualmode %= part->CVMModeCount;

								if (part->visualmode == part->CVMNormal) {
								    remove(*part, m_partHighlighted);
								}
								*/
							} // if hit
						}     // for each part on the board

						/*
						 * If we aren't holding down CTRL and we click to a
						 * non pin, non part area, then we clear everything
						 */
						if ((!any_hits) && (!io.KeyCtrl)) {
							for (auto &part : m_board->Components()) {
								//								auto p        = part.get();
								part->visualmode = part->CVMNormal;
							}
							m_partHighlighted.clear();
						}

					} // if a pin wasn't selected

				} else {
					if (!m_showContextMenu) {
						if (AnnotationIsHovered()) {
							m_needsRedraw        = true;
							AnnotationWasHovered = true;
						} else {
							AnnotationWasHovered = false;
							m_needsRedraw        = true;
						}
					}
				}

				m_draggingLastFrame = false;
			}

			// Zoom:
			float mwheel = io.MouseWheel;
			if (mwheel != 0.0f) {
				mwheel *= zoomFactor;

				Zoom(io.MousePos.x, io.MousePos.y, mwheel);
			}
		}
	}

	if ((!io.WantCaptureKeyboard)) {

		if (keybindings.isPressed("Mirror")) {
			Mirror();
			CenterView();
			m_needsRedraw = true;

		} else if (keybindings.isPressed("RotateCW")) {
			// Rotate board: R and period rotate clockwise; comma rotates
			// counter-clockwise
			Rotate(1);

		} else if (keybindings.isPressed("RotateCCW")) {
			Rotate(-1);

		} else if (keybindings.isPressed("ZoomIn")) {
			Zoom(m_board_surface.x / 2, m_board_surface.y / 2, zoomFactor);

		} else if (keybindings.isPressed("ZoomOut")) {
			Zoom(m_board_surface.x / 2, m_board_surface.y / 2, -zoomFactor);

		} else if (keybindings.isPressed("PanDown")) {
			Pan(DIR_DOWN, panFactor);

		} else if (keybindings.isPressed("PanUp")) {
			Pan(DIR_UP, panFactor);

		} else if (keybindings.isPressed("PanLeft")) {
			Pan(DIR_LEFT, panFactor);

		} else if (keybindings.isPressed("PanRight")) {
			Pan(DIR_RIGHT, panFactor);

		} else if (keybindings.isPressed("Center")) {
			// Center and reset zoom
			CenterView();

		} else if (keybindings.isPressed("Quit")) {
			// quit OFBV
			m_wantsQuit = true;

		} else if (keybindings.isPressed("InfoPanel")) {
			showInfoPanel = !showInfoPanel;
			obvconfig.WriteBool("showInfoPanel", showInfoPanel ? true : false);

		} else if (keybindings.isPressed("NetList")) {
			// Show Net List
			m_showNetList = m_showNetList ? false : true;

		} else if (keybindings.isPressed("PartList")) {
			// Show Part List
			m_showPartList = m_showPartList ? false : true;

		} else if (keybindings.isPressed("Flip")) {
			// Flip board:
			FlipBoard();

		} else if (keybindings.isPressed("TogglePins")) {
			showPins ^= 1;
			m_needsRedraw = true;

		} else if (keybindings.isPressed("Search")) {
			if (m_validBoard) {
				m_showSearch  = true;
				m_needsRedraw = true;
			}

		} else if (keybindings.isPressed("Clear")) {
			ClearAllHighlights();

		} else {
			/*
			 * Do what ever is required for unhandled key presses.
			 */
		}
	}
}

/* END UPDATE REGION */

/** Overlay and Windows
 *
 *
 */
void BoardView::ShowNetList(bool *p_open) {
	static NetList netList(bind(&BoardView::FindNet, this, _1));
	netList.Draw("Net List", p_open, m_board);
}

void BoardView::ShowPartList(bool *p_open) {
	static PartList partList(bind(&BoardView::FindComponent, this, _1));
	partList.Draw("Part List", p_open, m_board);
}

void BoardView::RenderOverlay() {

	ShowInfoPane();

	// Listing of Net elements
	if (m_showNetList) {
		ShowNetList(&m_showNetList);
	}
	if (m_showPartList) {
		ShowPartList(&m_showPartList);
	}
}

/** End overlay & windows region **/

/**
 * Drawing region
 *
 *
 *
 */

void BoardView::CenterZoomNet(string netname) {
	ImVec2 view = m_board_surface;
	ImVec2 min, max;
	int i = 0;

	if (!infoPanelCenterZoomNets) return;

	min.x = min.y = FLT_MAX;
	max.x = max.y = FLT_MIN;

	for (auto &pin : m_board->Pins()) {
		if (pin->net->name == netname) {
			auto p = pin->position;
			if (p.x < min.x) min.x = p.x;
			if (p.y < min.y) min.y = p.y;
			if (p.x > max.x) max.x = p.x;
			if (p.y > max.y) max.y = p.y;
			if (!infoPanelSelectPartsOnNet || pin->type == Pin::kPinTypeTestPad) continue;
			auto& cpt = pin->component;
			if (contains(cpt, m_partHighlighted)) continue;
			if (infoPanelSelectPartsOnNetOnlyNotGround) {
				auto has_ground = std::any_of(cpt->pins.cbegin(), cpt->pins.cend(), [](auto& pin) {
					return pin->net->is_ground;
				});
				if (has_ground && cpt->pins.size() == 2 && cpt->component_type == Component::kComponentTypeCapacitor) {
					continue;
				}
			}
			cpt->visualmode = cpt->CVMSelected;
			m_partHighlighted.push_back(cpt);
		}
	}

	// Bounds check!
	if ((min.x == FLT_MAX) || (min.y == FLT_MAX) || (max.x == FLT_MIN) || (max.y == FLT_MIN)) return;

	if (debug) fprintf(stderr, "CenterzoomNet: bbox[%d]: %0.1f %0.1f - %0.1f %0.1f\n", i, min.x, min.y, max.x, max.y);

	float dx = (max.x - min.x);
	float dy = (max.y - min.y);
	float sx = dx > 0 ? view.x / dx : FLT_MAX;
	float sy = dy > 0 ? view.y / dy : FLT_MAX;

	m_scale = sx < sy ? sx : sy;
	if (m_scale == FLT_MAX) m_scale = m_scale_floor;
	m_scale /= partZoomScaleOutFactor;
	if (m_scale < m_scale_floor) m_scale = m_scale_floor;

	/*
	float dx = (max.x - min.x);
	float dy = (max.y - min.y);
	float sx = dx > 0 ? view.x / dx : 1.0f;
	float sy = dy > 0 ? view.y / dy : 1.0f;

	//  m_rotation = 0;
	m_scale = sx < sy ? sx : sy;
	m_scale /= partZoomScaleOutFactor;
	if (m_scale < m_scale_floor) m_scale = m_scale_floor;
	*/

	m_dx = (max.x - min.x) / 2 + min.x;
	m_dy = (max.y - min.y) / 2 + min.y;
	SetTarget(m_dx, m_dy);
	m_needsRedraw = true;
}

void BoardView::CenterZoomSearchResults(void) {
	// ImVec2 view = ImGui::GetIO().DisplaySize;
	ImVec2 view = m_board_surface;
	ImVec2 min, max;
	int i = 0;

	if (!showPins) showPins = true; // Louis Rossmann UI failure fix.

	if (!m_centerZoomSearchResults) return;

	min.x = min.y = FLT_MAX;
	max.x = max.y = FLT_MIN;

	for (auto &pp : m_pinHighlighted) {
		auto &p = pp->position;
		if (p.x < min.x) min.x = p.x;
		if (p.y < min.y) min.y = p.y;
		if (p.x > max.x) max.x = p.x;
		if (p.y > max.y) max.y = p.y;
		i++;
	}

	for (auto &pp : m_partHighlighted) {
		for (auto &pn : pp->pins) {
			auto &p = pn->position;
			if (p.x < min.x) min.x = p.x;
			if (p.y < min.y) min.y = p.y;
			if (p.x > max.x) max.x = p.x;
			if (p.y > max.y) max.y = p.y;
			i++;
		}
	}

	// Bounds check!
	if ((min.x == FLT_MAX) || (min.y == FLT_MAX) || (max.x == FLT_MIN) || (max.y == FLT_MIN)) return;

	if (debug) fprintf(stderr, "CenterzoomSearchResults: bbox[%d]: %0.1f %0.1f - %0.1f %0.1f\n", i, min.x, min.y, max.x, max.y);
	// fprintf(stderr, "CenterzoomSearchResults: bbox[%d]: %0.1f %0.1f - %0.1f %0.1f\n", i, min.x, min.y, max.x, max.y);

	float dx = (max.x - min.x);
	float dy = (max.y - min.y);
	float sx = dx > 0 ? view.x / dx : FLT_MAX;
	float sy = dy > 0 ? view.y / dy : FLT_MAX;

	m_scale = sx < sy ? sx : sy;
	if (m_scale == FLT_MAX) m_scale = m_scale_floor;
	m_scale /= partZoomScaleOutFactor;
	if (m_scale < m_scale_floor) m_scale = m_scale_floor;

	m_dx = (max.x - min.x) / 2 + min.x;
	m_dy = (max.y - min.y) / 2 + min.y;
	SetTarget(m_dx, m_dy);
	m_needsRedraw = true;
}

/*
 * EPC = External Pin Count; finds pins which are not contained within
 * the outline and flips the board outline if required, as it seems some
 * brd2 files are coming with a y-flipped outline
 */
int BoardView::EPCCheck(void) {
	int epc[2] = {0, 0};
	int side;
	auto &outline = m_board->OutlinePoints();
	ImVec2 min, max;

	if (outline.empty()) {
		return 1;
	};

	// find the orthagonal bounding box
	// probably can put this as a predefined
	min.x = min.y = FLT_MAX;
	max.x = max.y = FLT_MIN;
	for (auto &p : outline) {
		if (p->x < min.x) min.x = p->x;
		if (p->y < min.y) min.y = p->y;
		if (p->x > max.x) max.x = p->x;
		if (p->y > max.y) max.y = p->y;
	}

	for (side = 0; side < 2; side++) {
		for (auto &p : m_board->Pins()) {
			// auto p = pin.get();
			int l, r;
			int jump = 1;
			Point fp;

			l = 0;
			r = 0;

			for (size_t i = 0; i < outline.size() - 1; i++) {
				Point &pa = *outline[i];
				Point &pb = *outline[i + 1];

				// jump double/dud points
				if (pa.x == pb.x && pa.y == pb.y) continue;

				// if we encounter our hull/poly start point, then we've now created the
				// closed
				// hull, jump the next segment and reset the first-point
				if ((!jump) && (fp.x == pb.x) && (fp.y == pb.y)) {
					if (i < outline.size() - 2) {
						fp   = *outline[i + 2];
						jump = 1;
						i++;
					}
				} else {
					jump = 0;
				}

				// test to see if this segment makes the scan-cut.
				if ((pa.y > pb.y && p->position.y < pa.y && p->position.y > pb.y) ||
				    (pa.y < pb.y && p->position.y > pa.y && p->position.y < pb.y)) {
					ImVec2 intersect;

					intersect.y = p->position.y;
					if (pa.x == pb.x)
						intersect.x = pa.x;
					else
						intersect.x = (pb.x - pa.x) / (pb.y - pa.y) * (p->position.y - pa.y) + pa.x;

					if (intersect.x > p->position.x)
						r++;
					else if (intersect.x < p->position.x)
						l++;
				}
			} // if we did get an intersection

			// If either side has no intersections, then it's out of bounds (likely)
			if ((l % 2 == 0) && (r % 2 == 0)) epc[side]++;
		} // pins

		if (debug) fprintf(stderr, "EPC[%d]: %d\n", side, epc[side]);

		// flip the outline
		for (auto &p : outline) p->y = max.y - p->y;

	} // side

	if ((epc[0] || epc[1]) && (epc[0] > epc[1])) {
		for (auto &p : outline) p->y = max.y - p->y;
	}

	return 0;
}

/*
 * Experimenting to see how much CPU hit rescanning and
 * drawing the flood fill is (as pin-stripe) each time
 * as opposed to precalculated line list
 *
 * We also do this slightly differently, we ask for a
 * y-pixel delta and thickness of line
 *
 */
void BoardView::OutlineGenFillDraw(ImDrawList *draw, int ydelta, double thickness = 1.0f) {

	auto io = ImGui::GetIO();
	vector<ImVec2> scanhits;
	static ImVec2 min, max; // board min/max points
	                        //	double steps = 500;
	double vdelta;
	double y, ystart, yend;

	if (!boardFill || slowCPU) return;
	if (!m_file) return;

	scanhits.reserve(20);

	draw->ChannelsSetCurrent(kChannelFill);

	// find the orthagonal bounding box
	// probably can put this as a predefined
	if (!boardMinMaxDone) {
		min.x = min.y = 100000000000;
		max.x = max.y = -100000000000;
		for (auto &p : m_board->OutlinePoints()) {
			if (p->x < min.x) min.x = p->x;
			if (p->y < min.y) min.y = p->y;
			if (p->x > max.x) max.x = p->x;
			if (p->y > max.y) max.y = p->y;
		}
		for (auto &s: m_board->OutlineSegments()) {
			if (s.first.x < min.x) min.x = s.first.x;
			if (s.second.x < min.x) min.x = s.second.x;
			if (s.first.y < min.y) min.y = s.first.y;
			if (s.second.y < min.y) min.y = s.second.y;
			if (s.first.x > max.x) max.x = s.first.x;
			if (s.second.x > max.x) max.x = s.second.x;
			if (s.first.y > max.y) max.y = s.first.y;
			if (s.second.y > max.y) max.y = s.second.y;
		}
		boardMinMaxDone = true;
	}

	// Get the viewport limits, so we don't waste time scanning what we don't need
	ImVec2 vpa = ScreenToCoord(0, 0);
	ImVec2 vpb = ScreenToCoord(io.DisplaySize.x, io.DisplaySize.y);

	if (vpa.y > vpb.y) {
		ystart = vpb.y;
		yend   = vpa.y;
	} else {
		ystart = vpa.y;
		yend   = vpb.y;
	}

	if (ystart < min.y) ystart = min.y;
	if (yend > max.y) yend = max.y;

	//	vdelta = (ystart - yend) / steps;

	vdelta = ydelta / m_scale;

	/*
	 * Go through each scan line
	 */
	y = ystart;
	while (y < yend) {

		scanhits.resize(0);
		// scan outline segments to see if any intersect with our horizontal scan line

		/*
		 * While we haven't yet exhausted possible scan hits
		 */

		{

			int jump = 1;
			Point fp;

			auto &outline_points = m_board->OutlinePoints();

			// set our initial draw point, so we can detect when we encounter it again
			if (!outline_points.empty()) {
				fp = *outline_points[0];

				for (size_t i = 0; i < outline_points.size() - 1; i++) {
					Point &pa = *outline_points[i];
					Point &pb = *outline_points[i + 1];

					// jump double/dud points
					if (pa.x == pb.x && pa.y == pb.y) continue;

					// if we encounter our hull/poly start point, then we've now created the
					// closed
					// hull, jump the next segment and reset the first-point
					if ((!jump) && (fp.x == pb.x) && (fp.y == pb.y)) {
						if (i < outline_points.size() - 2) {
							fp   = *outline_points[i + 2];
							jump = 1;
							i++;
						}
					} else {
						jump = 0;
					}

					// test to see if this segment makes the scan-cut.
					if ((pa.y > pb.y && y < pa.y && y > pb.y) || (pa.y < pb.y && y > pa.y && y < pb.y)) {
						ImVec2 intersect;

						intersect.y = y;
						if (pa.x == pb.x)
							intersect.x = pa.x;
						else
							intersect.x = (pb.x - pa.x) / (pb.y - pa.y) * (y - pa.y) + pa.x;
						scanhits.push_back(intersect);
					}
				} // if we did get an intersection
			}

			for (auto &s: m_board->OutlineSegments()) {
				auto pa = s.first;
				auto pb = s.second;

				// jump double/dud segments
				if (pa.x == pb.x && pa.y == pb.y) continue;

				// test to see if this segment makes the scan-cut.
				if ((pa.y > pb.y && y < pa.y && y > pb.y) || (pa.y < pb.y && y > pa.y && y < pb.y)) {
					ImVec2 intersect;

					intersect.y = y;
					if (pa.x == pb.x)
						intersect.x = pa.x;
					else
						intersect.x = (pb.x - pa.x) / (pb.y - pa.y) * (y - pa.y) + pa.x;
					scanhits.push_back(intersect);
				}
			}

			sort(scanhits.begin(), scanhits.end(), [](ImVec2 const &a, ImVec2 const &b) { return a.x < b.x; });
			// Some boards contain duplicate outline segments (possibly with points swapped) that generate duplicate intersections
			// which interferes with the process of generating alterating segments for the scanlines, so remove duplicates.
			scanhits.erase(std::unique(scanhits.begin(), scanhits.end(), [](const ImVec2 &a, const ImVec2 &b) {return a.x == b.x && a.y == b.y;}), scanhits.end());

			// now finally generate the lines.
			{
				int i = 0;
				int l = scanhits.size() - 1;
				for (i = 0; i < l; i += 2) {
					draw->AddLine(
					    CoordToScreen(scanhits[i].x, y), CoordToScreen(scanhits[i + 1].x, y), m_colors.boardFillColor, thickness);
				}
			}
		}
		y += vdelta;
	} // for each scan line
}

void BoardView::DrawDiamond(ImDrawList *draw, ImVec2 c, double r, uint32_t color) {
	ImVec2 dia[4];

	dia[0] = ImVec2(c.x, c.y - r);
	dia[1] = ImVec2(c.x + r, c.y);
	dia[2] = ImVec2(c.x, c.y + r);
	dia[3] = ImVec2(c.x - r, c.y);

	draw->AddPolyline(dia, 4, color, true, 1.0f);
}

void BoardView::DrawHex(ImDrawList *draw, ImVec2 c, double r, uint32_t color) {
	double da, db;
	ImVec2 hex[6];

	da = r * 0.5;         // cos(60')
	db = r * 0.866025404; // sin(60')

	hex[0] = ImVec2(c.x - r, c.y);
	hex[1] = ImVec2(c.x - da, c.y - db);
	hex[2] = ImVec2(c.x + da, c.y - db);
	hex[3] = ImVec2(c.x + r, c.y);
	hex[4] = ImVec2(c.x + da, c.y + db);
	hex[5] = ImVec2(c.x - da, c.y + db);

	draw->AddPolyline(hex, 6, color, true, 1.0f);
}

void BoardView::DrawOutlineSegments(ImDrawList *draw) {
	const auto &segments = m_board->OutlineSegments();

	draw->ChannelsSetCurrent(kChannelPolylines);

	for (auto &segment: segments) {
		ImVec2 spa = CoordToScreen(segment.first.x, segment.first.y);
		ImVec2 spb = CoordToScreen(segment.second.x, segment.second.y);

		/*
		 * If we have a pin selected, we mask off the colour to shade out
		 * things and make it easier to see associated pins/points
		 */
		if ((pinSelectMasks) && (m_pinSelected || m_pinHighlighted.size())) {
			draw->AddLine(spa, spb, (m_colors.boardOutlineColor & m_colors.selectedMaskOutline) | m_colors.orMaskOutline);
		} else {
			draw->AddLine(spa, spb, m_colors.boardOutlineColor);
		}
	}
}

void BoardView::DrawOutlinePoints(ImDrawList *draw) {
	int jump = 1;
	Point fp;

	auto &outline = m_board->OutlinePoints();
	if (outline.size() < 1) { // Nothing to draw
		return;
	}

	draw->ChannelsSetCurrent(kChannelPolylines);

	// set our initial draw point, so we can detect when we encounter it again
	fp = *outline[0];

	draw->PathClear();
	for (size_t i = 0; i < outline.size() - 1; i++) {
		Point &pa = *outline[i];
		Point &pb = *outline[i + 1];

		// jump double/dud points
		if (pa.x == pb.x && pa.y == pb.y) continue;

		// if we encounter our hull/poly start point, then we've now created the
		// closed
		// hull, jump the next segment and reset the first-point
		if ((!jump) && (fp.x == pb.x) && (fp.y == pb.y)) {
			if (i < outline.size() - 2) {
				fp   = *outline[i + 2];
				jump = 1;
				i++;
			}
		} else {
			jump = 0;
		}

		ImVec2 spa = CoordToScreen(pa.x, pa.y);
		ImVec2 spb = CoordToScreen(pb.x, pb.y);

		/*
		 * If we have a pin selected, we mask off the colour to shade out
		 * things and make it easier to see associated pins/points
		 */
		if ((pinSelectMasks) && (m_pinSelected || m_pinHighlighted.size())) {
			draw->AddLine(spa, spb, (m_colors.boardOutlineColor & m_colors.selectedMaskOutline) | m_colors.orMaskOutline);
		} else {
			draw->AddLine(spa, spb, m_colors.boardOutlineColor);
		}
	} // for
}

void BoardView::DrawOutline(ImDrawList *draw) {
	DrawOutlineSegments(draw);
	DrawOutlinePoints(draw);
}

void BoardView::DrawNetWeb(ImDrawList *draw) {
	if (!showNetWeb) return;

	/*
	 * Some nets we don't bother showing, because they're not relevant or
	 * produce too many results (such as ground!)
	 */
	if (m_pinSelected->type == Pin::kPinTypeNotConnected) return;
	if (m_pinSelected->type == Pin::kPinTypeUnkown) return;
	if (m_pinSelected->net->is_ground) return;

	for (auto &p : m_board->Pins()) {

		if (p->net == m_pinSelected->net) {
			uint32_t col = m_colors.pinNetWebColor;
			if (!BoardElementIsVisible(p->component)) {
				col = m_colors.pinNetWebOSColor;
				draw->AddCircle(CoordToScreen(p->position.x, p->position.y), p->diameter * m_scale, col, 16);
			}

			draw->AddLine(CoordToScreen(m_pinSelected->position.x, m_pinSelected->position.y),
			              CoordToScreen(p->position.x, p->position.y),
			              ImColor(col),
			              netWebThickness);
		}
	}

	return;
}

static Pin* InferPin(Pin* pin, string Pin::* show_value_ptr) {
	auto iter = std::find_if(pin->net->pins.cbegin(), pin->net->pins.cend(), [show_value_ptr](auto& opin) {
						return !((*opin).*show_value_ptr).empty();
					});
	if (iter != pin->net->pins.cend())
		return (*iter).get();
	return nullptr;
}

inline void BoardView::DrawPins(ImDrawList *draw) {

	uint32_t cmask  = 0xFFFFFFFF;
	uint32_t omask  = 0x00000000;
	float threshold = 0;
	auto io         = ImGui::GetIO();

	if (!showPins) return;

	/*
	 * If we have a pin selected, then it makes it
	 * easier to see where the associated pins are
	 * by masking out (alpha or channel) the other
	 * pins so they're fainter.
	 */
	if (pinSelectMasks) {
		if (m_pinSelected || m_pinHighlighted.size()) {
			cmask = m_colors.selectedMaskPins;
			omask = m_colors.orMaskPins;
		}
	}

	if (slowCPU) {
		threshold      = 2.0f;
	}
	if (pinSizeThresholdLow > threshold) threshold = pinSizeThresholdLow;

	draw->ChannelsSetCurrent(kChannelPins);

	if (m_pinSelected) DrawNetWeb(draw);

	for (auto &pin : m_board->Pins()) {
		float psz           = pin->diameter * m_scale;
		uint32_t fill_color = 0xFFFF8888; // fallback fill colour
		uint32_t text_color = m_colors.pinDefaultTextColor;
		uint32_t color      = (m_colors.pinDefaultColor & cmask) | omask;
		bool fill_pin       = false;
		bool show_text      = false;
		bool draw_ring      = true;
		bool show_net_name  = true;

		// continue if pin is not visible anyway
		if (!BoardElementIsVisible(pin)) continue;

		ImVec2 pos = CoordToScreen(pin->position.x, pin->position.y);
		{
			if (!IsVisibleScreen(pos.x, pos.y, psz, io)) continue;
		}

		if ((!m_pinSelected) && (psz < threshold)) continue;

		// color & text depending on app state & pin type

		{
			/*
			 * Pins resulting from a net search
			 */
			if (contains(pin, m_pinHighlighted)) {
				if (psz < fontSize / 2) psz = fontSize / 2;
				text_color = m_colors.pinSelectedTextColor;
				fill_color = m_colors.pinSelectedFillColor;
				color      = m_colors.pinSelectedColor;
				// text_color = color = m_colors.pinSameNetColor;
				fill_pin  = true;
				show_text = true;
				draw_ring = true;
				threshold = 0;
				//				draw->AddCircle(ImVec2(pos.x, pos.y), psz * pinHaloDiameter, ImColor(0xff0000ff), 32);
			}

			/*
			 * If the part is selected, as part of search or otherwise
			 */
			if (PartIsHighlighted(pin->component)) {
				color      = m_colors.pinDefaultColor;
				text_color = m_colors.pinDefaultTextColor;
				fill_pin   = false;
				draw_ring  = true;
				show_text  = true;
				threshold  = 0;
			}

			if (pin->type == Pin::kPinTypeTestPad) {
				color      = (m_colors.pinTestPadColor & cmask) | omask;
				fill_color = (m_colors.pinTestPadFillColor & cmask) | omask;
				show_text  = false;
			}

			// If the part itself is highlighted ( CVMShowPins )
			// if (p_pin->component->visualmode == p_pin->component->CVMSelected) {
			if (pin->component->visualmode == pin->component->CVMSelected) {
				color      = m_colors.pinDefaultColor;
				text_color = m_colors.pinDefaultTextColor;
				fill_pin   = false;
				draw_ring  = true;
				show_text  = true;
				threshold  = 0;
			}

			if (!pin->net || pin->type == Pin::kPinTypeNotConnected) {
				color = (m_colors.pinNotConnectedColor & cmask) | omask;
				fill_pin = true;
				fill_color = color;
				show_net_name = false;
			}

			// pin is on the same net as selected pin: highlight > rest
			if (m_pinSelected && pin->net == m_pinSelected->net) {
				if (psz < fontSize / 2) psz = fontSize / 2;
				color      = m_colors.pinSameNetColor;
				text_color = m_colors.pinSameNetTextColor;
				fill_color = m_colors.pinSameNetFillColor;
				draw_ring  = false;
				fill_pin   = true;
				show_text  = true; // is this something we want? Maybe an optional thing?
				threshold  = 0;
			}

			// pin selected overwrites everything
			// if (p_pin == m_pinSelected) {
			if (pin == m_pinSelected) {
				if (psz < fontSize / 2) psz = fontSize / 2;
				color      = m_colors.pinSelectedColor;
				text_color = m_colors.pinSelectedTextColor;
				fill_color = m_colors.pinSelectedFillColor;
				draw_ring  = false;
				show_text  = true;
				fill_pin   = true;
				threshold  = 0;
			}

			//ground pin
			if (pin->net->is_ground) {
				color = m_colors.pinGroundColor;
				fill_color = color;
				fill_pin = true;
				show_net_name = false;
			}
			// Check for BGA pin '1'
			//
			if (pin->name == "A1") {
				color = fill_color = m_colors.pinA1PadColor;
				fill_pin           = m_colors.pinA1PadColor;
				draw_ring          = false;
			}

			if ((pin->number == "1")) {
				if (pin->component->pins.size() >= static_cast<unsigned int>(pinA1threshold)) { // pinA1threshold is never negative
					color = fill_color = m_colors.pinA1PadColor;
					fill_pin           = m_colors.pinA1PadColor;
					draw_ring          = false;
				}
			}

			if (pin->voltage_flag != PinVoltageFlag::unknown) {
				
			}

			// don't show text if it doesn't make sense
			if (pin->component->pins.size() <= 1) show_text = false;
			if (pin->type == Pin::kPinTypeTestPad) show_text = false;
		}

		// Drawing
		{
			int segments;
			//			draw->ChannelsSetCurrent(kChannelImages);

			// for the round pin representations, choose how many circle segments need
			// based on the pin size
			segments = round(psz);
			if (segments > 32) segments = 32;
			if (segments < 8) segments = 8;
			float h = psz / 2 + 0.5f;
			float w = h;
			if (pin->shape == kShapeTypeRect) {
				w = pin->size.x * m_scale / 2 + 0.5f;
				h = pin->size.y * m_scale / 2 + 0.5f;
			}
			if (pin->angle == 90 || pin->angle == 270) {
				std::swap(w, h);
			}

			/*
			 * if we're going to be showing the text of a pin, then we really
			 * should make sure that the drawn pin is at least as big as a single
			 * character so it doesn't look messy
			 */
			if ((show_text) && (psz < fontSize / 2)) psz = fontSize / 2;

			switch (pin->type) {
				case Pin::kPinTypeTestPad:
					if ((psz > 3) && (!slowCPU)) {
						draw->AddCircleFilled(ImVec2(pos.x, pos.y), psz, fill_color, segments);
						draw->AddCircle(ImVec2(pos.x, pos.y), psz, color, segments);
					} else if (psz > threshold) {
						draw->AddRectFilled(ImVec2(pos.x - w, pos.y - h), ImVec2(pos.x + w, pos.y + h), fill_color);
					}
					break;
				default:
					if ((psz > 3) && (psz > threshold)) {
						if (pinShapeSquare || slowCPU || pin->shape == kShapeTypeRect) {
							if (fill_pin)
								draw->AddRectFilled(ImVec2(pos.x - w, pos.y - h), ImVec2(pos.x + w, pos.y + h),  fill_color);
							if (draw_ring) draw->AddRect(ImVec2(pos.x - w, pos.y - h), ImVec2(pos.x + w, pos.y + h), color);
						} else {
							if (fill_pin) draw->AddCircleFilled(ImVec2(pos.x, pos.y), psz, fill_color, segments);
							if (draw_ring) draw->AddCircle(ImVec2(pos.x, pos.y), psz, color, segments);
						}
					} else if (psz > threshold) {
						if (fill_pin) draw->AddRectFilled(ImVec2(pos.x - w, pos.y - h), ImVec2(pos.x + w, pos.y + h), fill_color);
						if (draw_ring) draw->AddRect(ImVec2(pos.x - w, pos.y - h), ImVec2(pos.x + w, pos.y + h), color);
					}
			}

			// if (p_pin == m_pinSelected) {
			//		if (pin.get() == m_pinSelected) {
			//			draw->AddCircle(ImVec2(pos.x, pos.y), psz + 1.25, m_colors.pinSelectedTextColor, segments);
			//		}

			//		if ((color == m_colors.pinSameNetColor) && (pinHalo == true)) {
			//			draw->AddCircle(ImVec2(pos.x, pos.y), psz * pinHaloDiameter, m_colors.pinHaloColor, segments,
			// pinHaloThickness);
			//		}

			// Show all pin names when showPinName is enabled and pin diameter is above threshold or show pin name only for selected part
			if ((showPinName && psz > 3) || show_text) {
				std::string text = pin->name + "\n" + pin->net->name;
				ImFont *font = ImGui::GetIO().Fonts->Fonts[0]; // Default font
				ImVec2 text_size_normalized = font->CalcTextSizeA(1.0f, FLT_MAX, 0.0f, text.c_str());

				float maxfontwidth = psz * 2.125/ text_size_normalized.x; // Fit horizontally with 6.75% overflow (should still avoid colliding with neighbours)
				float maxfontheight = psz * 1.5/ text_size_normalized.y; // Fit vertically with 25% top/bottom padding
				float maxfontsize = min(maxfontwidth, maxfontheight);

				// Font size for pin name only depends on height of text (rather than width of full text incl. net name) to scale to pin bounding box
				ImVec2 size_pin_name = font->CalcTextSizeA(maxfontheight, FLT_MAX, 0.0f, pin->name.c_str());
				// Font size for net name also depends on width of full text to avoid overflowing too much and colliding with text from other pin
				ImVec2 size_net_name = font->CalcTextSizeA(maxfontsize, FLT_MAX, 0.0f, pin->net->name.c_str());

				string show_value;
				if (showMode != ShowMode_None) {
					auto show_value_ptr = [this]{
						switch (showMode) {
							case ShowMode_Ohm:
								return &Pin::ohm_value;
							case ShowMode_Voltage:
								return &Pin::voltage_value;
							case ShowMode_Diode:
								return &Pin::diode_value;
							default:
								return &Pin::diode_value;
						}
					}();
					show_value = ((*pin).*show_value_ptr);
					if (show_value.empty() && inferValue) {
						auto inferPin = InferPin(pin.get(), show_value_ptr);
						if (inferPin) {
							show_value = ((*inferPin).*show_value_ptr);
							show_value += " (";
							show_value += inferPin->component->name;
							show_value += ")";
						}
					}
				}
				ImVec2 size_show_value = font->CalcTextSizeA(maxfontheight, FLT_MAX, 0.0f, show_value.c_str());

				// Show pin name above net name, full text is centered vertically
				ImVec2 pos_pin_name   = ImVec2(pos.x - size_pin_name.x * 0.5f, pos.y - size_pin_name.y);
				ImVec2 pos_net_name   = ImVec2(pos.x - size_net_name.x * 0.5f, pos.y);
				ImVec2 pos_show_value = ImVec2(pos.x - size_show_value.x*0.5f, pos_pin_name.y - size_show_value.y);

				ImFont *font_pin_name = font;
				if (maxfontheight < font->FontSize * 0.75) {
					font_pin_name = ImGui::GetIO().Fonts->Fonts[2]; // Use smaller font for pin name
				} else if (maxfontheight > font->FontSize * 1.5 && ImGui::GetIO().Fonts->Fonts[1]->FontSize > font->FontSize) {
					font_pin_name = ImGui::GetIO().Fonts->Fonts[1]; // Use larger font for pin name
				}

				ImFont *font_net_name = font;
				if (maxfontsize < font->FontSize * 0.75) {
					font_net_name = ImGui::GetIO().Fonts->Fonts[2]; // Use smaller font for net name
				} else if (maxfontsize > font->FontSize * 1.5 && ImGui::GetIO().Fonts->Fonts[1]->FontSize > font->FontSize) {
					font_net_name = ImGui::GetIO().Fonts->Fonts[1]; // Use larger font for net name
				}


				ImFont *font_show_value = font;
				if (maxfontsize < font->FontSize * 0.75) {
					font_show_value = ImGui::GetIO().Fonts->Fonts[2]; // Use smaller font for net name
				} else if (maxfontsize > font->FontSize * 1.5 && ImGui::GetIO().Fonts->Fonts[1]->FontSize > font->FontSize) {
					font_show_value = ImGui::GetIO().Fonts->Fonts[1]; // Use larger font for net name
				}

				// Background rectangle
				if (show_net_name)
					draw->AddRectFilled(ImVec2(pos_net_name.x - m_scale * 0.5f, pos_net_name.y), // Begining of text with slight padding
											ImVec2(pos_net_name.x + size_net_name.x + m_scale * 0.5f, pos_net_name.y + size_net_name.y), // End of text with slight padding
											m_colors.pinTextBackgroundColor,
											m_scale * 0.5f/*rounding*/);

				draw->ChannelsSetCurrent(kChannelText);
				draw->AddText(font_pin_name, maxfontheight, pos_pin_name, text_color, pin->name.c_str());
				if (show_net_name)
					draw->AddText(font_net_name, maxfontsize, pos_net_name, text_color, pin->net->name.c_str());
				if (!show_value.empty()) {
					draw->AddText(font_show_value, maxfontheight, pos_show_value, m_colors.annotationBoxColor, show_value.c_str());
				}
				draw->ChannelsSetCurrent(kChannelPins);
			}
		}
	}
}

inline void BoardView::DrawParts(ImDrawList *draw) {
	// float psz = (float)m_pinDiameter * 0.5f * m_scale;
	double angle;
	double distance = 0;
	uint32_t color = m_colors.partOutlineColor;
	//	int rendered   = 0;
	char p0, p1; // first two characters of the part name, code-writing
	             // convenience more than anything else

	draw->ChannelsSetCurrent(kChannelPolylines);
	/*
	 * If a pin has been selected, we mask out the colour to
	 * enhance (relatively) the appearance of the pin(s)
	 */
	if (pinSelectMasks && ((m_pinSelected) || m_pinHighlighted.size())) {
		color = (m_colors.partOutlineColor & m_colors.selectedMaskParts) | m_colors.orMaskParts;
	}

	for (auto &part : m_board->Components()) {
		int pincount = 0;
		double min_x, min_y, max_x, max_y, aspect;
		std::vector<ImVec2> pva;
		std::array<ImVec2, 4> dbox; // default box, if there's nothing else claiming to render the part different.

		if (part->is_dummy()) continue;

		/*
		 *
		 * When we first load the board, the outline of each part isn't (yet) rendered
		 * or determined/calculated.  So, the first time we display the board we
		 * compute the outline and store it for later.
		 *
		 * This also sets the pin diameters too.
		 *
		 */
		if (!part->outline_done) { // should only need to do this once for most parts
			if (part->pins.size() == 0) {
				if (debug) fprintf(stderr, "WARNING: Drawing empty part %s\n", part->name.c_str());
				draw->AddRect(CoordToScreen(part->p1.x + DPIF(10), part->p1.y + DPIF(10)),
				              CoordToScreen(part->p2.x - DPIF(10), part->p2.y - DPIF(10)),
				              0xff0000ff);
				draw->AddText(
				    CoordToScreen(part->p1.x + DPIF(10), part->p1.y - DPIF(50)), m_colors.partTextColor, part->name.c_str());
				continue;
			}

			for (auto &pin : part->pins) {
				pincount++;

				// scale box around pins as a fallback, else either use polygon or convex
				// hull for better shape fidelity
				if (pincount == 1) {
					min_x = pin->position.x;
					min_y = pin->position.y;
					max_x = min_x;
					max_y = min_y;
				}

				pva.push_back({pin->position.x, pin->position.y});

				if (pin->position.x > max_x) {
					max_x = pin->position.x;

				} else if (pin->position.x < min_x) {
					min_x = pin->position.x;
				}
				if (pin->position.y > max_y) {
					max_y = pin->position.y;

				} else if (pin->position.y < min_y) {
					min_y = pin->position.y;
				}
			}

			part->omin        = ImVec2(min_x, min_y);
			part->omax        = ImVec2(max_x, max_y);
			part->centerpoint = ImVec2((max_x - min_x) / 2 + min_x, (max_y - min_y) / 2 + min_y);

			distance = sqrt((max_x - min_x) * (max_x - min_x) + (max_y - min_y) * (max_y - min_y));

			float pin_radius = m_pinDiameter / 2.0f;

			/*
			 *
			 * Determine the size of our part's pin radius based on the distance
			 * between the extremes of the pin coordinates.
			 *
			 * All the figures below are determined empirically rather than any
			 * specific formula.
			 *
			 */
			if ((pincount < 4) && (part->name[0] != 'U') && (part->name[0] != 'Q')) {

				if ((distance > 52) && (distance < 57)) {
					// 0603
					pin_radius = 15;
					for (auto &pin : part->pins) {
						pin->diameter = pin_radius; // * 0.05;
					}

				} else if ((distance > 247) && (distance < 253)) {
					// SMC diode?
					pin_radius = 50;
					for (auto &pin : part->pins) {
						pin->diameter = pin_radius; // * 0.05;
					}

				} else if ((distance > 195) && (distance < 199)) {
					// Inductor?
					pin_radius = 50;
					for (auto &pin : part->pins) {
						pin->diameter = pin_radius; // * 0.05;
					}

				} else if ((distance > 165) && (distance < 169)) {
					// SMB diode?
					pin_radius = 35;
					for (auto &pin : part->pins) {
						pin->diameter = pin_radius; // * 0.05;
					}

				} else if ((distance > 101) && (distance < 109)) {
					// SMA diode / tant cap
					pin_radius = 30;
					for (auto &pin : part->pins) {
						pin->diameter = pin_radius; // * 0.05;
					}

				} else if ((distance > 108) && (distance < 112)) {
					// 1206
					pin_radius = 30;
					for (auto &pin : part->pins) {
						pin->diameter = pin_radius; // * 0.05;
					}

				} else if ((distance > 64) && (distance < 68)) {
					// 0805
					pin_radius = 25;
					for (auto &pin : part->pins) {
						pin->diameter = pin_radius; // * 0.05;
					}

				} else if ((distance > 18) && (distance < 22)) {
					// 0201 cap/resistor?
					pin_radius = 5;
					for (auto &pin : part->pins) {
						pin->diameter = pin_radius; // * 0.05;
					}
				} else if ((distance > 28) && (distance < 32)) {
					// 0402 cap/resistor
					pin_radius = 10;
					for (auto &pin : part->pins) {
						pin->diameter = pin_radius; // * 0.05;
					}
				}
			}

			// TODO: pin radius is stored in Pin object
			//
			//
			//
			min_x -= pin_radius;
			max_x += pin_radius;
			min_y -= pin_radius;
			max_y += pin_radius;

			if ((max_y - min_y) < 0.01)
				aspect = 0;
			else
				aspect = (max_x - min_x) / (max_y - min_y);

			dbox[0].x = dbox[3].x = min_x;
			dbox[1].x = dbox[2].x = max_x;
			dbox[0].y = dbox[1].y = min_y;
			dbox[3].y = dbox[2].y = max_y;

			p0 = part->name[0];
			p1 = part->name[1];

			/*
			 * Draw all 2~3 pin devices as if they're not orthagonal.  It's a bit more
			 * CPU
			 * overhead but it keeps the code simpler and saves us replicating things.
			 */

			if ((pincount == 3) && (abs(aspect) > 0.5) &&
			    ((strchr("DQZ", p0) || (strchr("DQZ", p1)) || strcmp(part->name.c_str(), "LED")))) {

				part->outline = dbox;
				part->outline_done = true;

				part->hull.clear();
				for (auto &pin : part->pins) {
					part->hull.push_back({pin->position.x, pin->position.y});
				}

				/*
				 * handle all other devices not specifically handled above
				 */
			} else if ((pincount > 1) && (pincount < 4) && ((strchr("CRLD", p0) || (strchr("CRLD", p1))))) {
				double dx, dy;
				double tx, ty;
				double armx, army;

				dx    = part->pins[1]->position.x - part->pins[0]->position.x;
				dy    = part->pins[1]->position.y - part->pins[0]->position.y;
				angle = atan2(dy, dx);

				if (((p0 == 'L') || (p1 == 'L')) && (distance > 50)) {
					pin_radius = 15;
					for (auto &pin : part->pins) {
						pin->diameter = pin_radius; // * 0.05;
					}
					army = distance / 2;
					armx = pin_radius;
				} else if (((p0 == 'C') || (p1 == 'C')) && (distance > 90)) {
					double mpx, mpy;

					pin_radius = 15;
					for (auto &pin : part->pins) {
						pin->diameter = pin_radius; // * 0.05;
					}
					army = distance / 2 - distance / 4;
					armx = pin_radius;

					mpx = dx / 2 + part->pins[0]->position.x;
					mpy = dy / 2 + part->pins[0]->position.y;
					VHRotateV(&mpx, &mpy, dx / 2 + part->pins[0]->position.x, dy / 2 + part->pins[0]->position.y, angle);

					part->expanse        = distance;
					part->centerpoint.x  = mpx;
					part->centerpoint.y  = mpy;
					part->component_type = part->kComponentTypeCapacitor;

				} else {
					armx = army = pin_radius;
				}

				// TODO: Compact this bit of code, maybe. It works at least.
				tx = part->pins[0]->position.x - armx;
				ty = part->pins[0]->position.y - army;
				VHRotateV(&tx, &ty, part->pins[0]->position.x, part->pins[0]->position.y, angle);
				// a = CoordToScreen(tx, ty);
				part->outline[0].x = tx;
				part->outline[0].y = ty;

				tx = part->pins[0]->position.x - armx;
				ty = part->pins[0]->position.y + army;
				VHRotateV(&tx, &ty, part->pins[0]->position.x, part->pins[0]->position.y, angle);
				// b = CoordToScreen(tx, ty);
				part->outline[1].x = tx;
				part->outline[1].y = ty;

				tx = part->pins[1]->position.x + armx;
				ty = part->pins[1]->position.y + army;
				VHRotateV(&tx, &ty, part->pins[1]->position.x, part->pins[1]->position.y, angle);
				// c = CoordToScreen(tx, ty);
				part->outline[2].x = tx;
				part->outline[2].y = ty;

				tx = part->pins[1]->position.x + armx;
				ty = part->pins[1]->position.y - army;
				VHRotateV(&tx, &ty, part->pins[1]->position.x, part->pins[1]->position.y, angle);
				// d = CoordToScreen(tx, ty);
				part->outline[3].x = tx;
				part->outline[3].y = ty;

				part->outline_done = true;

				// rendered = 1;

			} else {

				/*
				 * If we have (typically) a connector with a non uniform pin distribution
				 * then we can try use the minimal bounding box algorithm
				 * to give it a more sane outline
				 */
				if ((pincount >= 4) && ((strchr("UJL", p0) || strchr("UJL", p1) || (strncmp(part->name.c_str(), "CN", 2) == 0)))) {
					// Find our hull
					std::vector<ImVec2> hull = VHConvexHull(pva);

					// If we had a valid hull, then find the MBB for it
					if (hull.size() > 0) {
						part->hull = hull;

						std::array<ImVec2, 4> bbox = VHMBBCalculate(hull, pin_radius);
						part->outline = bbox;
						part->outline_done = true;

						/*
						 * Tighten the hull, removes any small angle segments
						 * such as a sequence of pins in a line, might be an overkill
						 */
						// hpc = TightenHull(hull, hpc, 0.1f);
					}
				} else {
					// if it wasn't at an odd angle, or wasn't large, or wasn't a connector,
					// just an ordinary
					// type part, then this is where we'll likely end up
					part->outline = dbox;
					part->outline_done = true;
				}
			}

			//			if (rendered == 0) {
			//				fprintf(stderr, "Part wasn't rendered (%s)\n", part->name.c_str());
			//			}

			if (part->is_special_outline) {
				part->outline = part->special_outline;
			}
		} // if !outline_done

		if (!BoardElementIsVisible(part) && !PartIsHighlighted(part)) continue;

		if (part->outline_done) {

			/*
			 * Draw the bounding box for the part
			 */
			ImVec2 a, b, c, d;

			a = ImVec2(CoordToScreen(part->outline[0].x, part->outline[0].y));
			b = ImVec2(CoordToScreen(part->outline[1].x, part->outline[1].y));
			c = ImVec2(CoordToScreen(part->outline[2].x, part->outline[2].y));
			d = ImVec2(CoordToScreen(part->outline[3].x, part->outline[3].y));

			// if (fillParts) draw->AddQuadFilled(a, b, c, d, color & 0xffeeeeee);
			if (fillParts && !slowCPU) draw->AddQuadFilled(a, b, c, d, m_colors.partFillColor);
			draw->AddQuad(a, b, c, d, color);
			if (PartIsHighlighted(part)) {
				if (fillParts && !slowCPU) draw->AddQuadFilled(a, b, c, d, m_colors.partHighlightedFillColor);
				draw->AddQuad(a, b, c, d, m_colors.partHighlightedColor);
			}

			if (part->component_type == Component::kComponentTypeBoard) {
				draw->AddQuadFilled(a, b, c, d, m_colors.boardFillColor);
			}
			/*
			 * Draw the convex hull of the part if it has one
			 */
			if (!part->hull.empty()) {
				draw->PathClear();
				for (size_t i = 0; i < part->hull.size(); i++) {
					ImVec2 p = CoordToScreen(part->hull[i].x, part->hull[i].y);
					draw->PathLineTo(p);
				}
				draw->PathStroke(m_colors.partHullColor, true, 1.0f);
			}

			/*
			 * Draw any icon/mark featuers to illustrate the part better
			 */
			if (part->component_type == part->kComponentTypeCapacitor) {
				if (part->expanse > 90) {
					int segments = trunc(part->expanse);
					if (segments < 8) segments = 8;
					if (segments > 36) segments = 36;
					draw->AddCircle(CoordToScreen(part->centerpoint.x, part->centerpoint.y),
					                (part->expanse / 3) * m_scale,
					                m_colors.partOutlineColor & 0x8fffffff,
					                segments);
				}
			}

			if (!part->is_dummy() && !part->name.empty()) {
				const auto& text  = showPartType && !part->part_type.empty() ? part->part_type : part->name;

				/*
				 * Draw part name inside part bounding box
				 */
				if (showPartName) {
					ImFont *font = ImGui::GetIO().Fonts->Fonts[0]; // Default font
					ImVec2 text_size_normalized	= font->CalcTextSizeA(1.0f, FLT_MAX, 0.0f, text.c_str());

					// Find max width and height of bounding box, not perfect for non-straight bounding box but good enough
					float minx = std::min({a.x, b.x, c.x, d.x});
					float miny = std::min({a.y, b.y, c.y, d.y});
					float maxx = std::max({a.x, b.x, c.x, d.x});
					float maxy = std::max({a.y, b.y, c.y, d.y});
					float maxwidth = abs(maxx - minx) * 0.7; // Bounding box width with 30% padding
					float maxheight = abs(maxy - miny) * 0.7; // Bounding box height with 30% padding

					// Find max font size to fit text inside bounding box
					float maxfontwidth = maxwidth / text_size_normalized.x;
					float maxfontheight = maxheight / text_size_normalized.y;
					float maxfontsize = min(maxfontwidth, maxfontheight);

					ImVec2 text_size{text_size_normalized.x * maxfontsize, text_size_normalized.y * maxfontsize};

					// Center text
					ImVec2 pos = CoordToScreen(part->centerpoint.x, part->centerpoint.y); // Computed previously during bounding box generation
					pos.x -= text_size.x * 0.5f;
					pos.y -= text_size.y * 0.5f;

					if (maxfontsize < font->FontSize * 0.75) {
						font = ImGui::GetIO().Fonts->Fonts[2]; // Use smaller font for part name
					} else if (maxfontsize > font->FontSize * 1.5 && ImGui::GetIO().Fonts->Fonts[1]->FontSize > font->FontSize) {
						font = ImGui::GetIO().Fonts->Fonts[1]; // Use larger font for part name
					}

					draw->ChannelsSetCurrent(kChannelText);
					draw->AddText(font, maxfontsize, pos, m_colors.partTextColor, text.c_str());
					draw->ChannelsSetCurrent(kChannelPolylines);
				}

				/*
				 * Draw the highlighted text for selected part
				 */
				if (PartIsHighlighted(part)) {
					std::string mcode = part->mfgcode;

					ImVec2 text_size    = ImGui::CalcTextSize(text.c_str());
					ImVec2 mfgcode_size = ImGui::CalcTextSize(mcode.c_str());

					if ((!showInfoPanel) && (mfgcode_size.x > text_size.x)) text_size.x = mfgcode_size.x;

					float top_y = a.y;

					if (c.y < top_y) top_y = c.y;
					ImVec2 pos = ImVec2((a.x + c.x) * 0.5f, top_y);

					pos.y -= text_size.y * 2;
					if (mcode.size()) pos.y -= text_size.y;

					pos.x -= text_size.x * 0.5f;
					draw->ChannelsSetCurrent(kChannelText);

					// This is the background of the part text.
					draw->AddRectFilled(ImVec2(pos.x - DPIF(2.0f), pos.y - DPIF(2.0f)),
										ImVec2(pos.x + text_size.x + DPIF(2.0f), pos.y + text_size.y + DPIF(2.0f)),
										m_colors.partHighlightedTextBackgroundColor,
										0.0f);
					draw->AddText(pos, m_colors.partHighlightedTextColor, text.c_str());
					if ((!showInfoPanel) && (mcode.size())) {
						//	pos.y += text_size.y;
						pos.y += text_size.y + DPIF(2.0f);
						draw->AddRectFilled(ImVec2(pos.x - DPIF(2.0f), pos.y - DPIF(2.0f)),
											ImVec2(pos.x + text_size.x + DPIF(2.0f), pos.y + text_size.y + DPIF(2.0f)),
											m_colors.annotationPopupBackgroundColor,
											0.0f);
						draw->AddText(ImVec2(pos.x, pos.y), m_colors.annotationPopupTextColor, mcode.c_str());
					}
					draw->ChannelsSetCurrent(kChannelPolylines);
				}
			}
		}
	} // for each part
}

inline void BoardView::DrawTracks(ImDrawList *draw) {
	uint32_t cmask  = 0xFFFFFFFF;
	uint32_t omask  = 0x00000000;
	auto io         = ImGui::GetIO();

	if (pinSelectMasks) {
		if (m_pinSelected || m_pinHighlighted.size()) {
			cmask = m_colors.selectedMaskPins;
			omask = m_colors.orMaskPins;
		}
	}
	draw->ChannelsSetCurrent(kChannelPolylines);

	const auto& tracks = m_board->Tracks();
	for (const auto &track : tracks) {
		if (!(m_pinSelected && m_pinSelected->net == track->net) && !(m_viaSelected && m_viaSelected->net == track->net) && !BoardElementIsVisible(track)) continue;
		ImVec2 pos_start = CoordToScreen(track->position_start.x, track->position_start.y);
		ImVec2 pos_end = CoordToScreen(track->position_end.x, track->position_end.y);

		uint32_t color      = (m_colors.layerColor[track->board_side][0] & cmask) | omask;
		auto radius = track->width * m_scale;
		if ((m_pinSelected && m_pinSelected->net == track->net) || (m_viaSelected && m_viaSelected->net == track->net)) {
			color      = m_colors.layerColor[track->board_side][1];
			draw->AddLine(pos_start, pos_end, m_colors.defaultBoardSelectColor, radius*2);
		}
		draw->AddLine(pos_start, pos_end, color, radius);
	}
}

static void DrawArc(ImDrawList* draw_list, ImVec2 center, float radius, ImU32 color, float start_angle, float end_angle, int num_segments = 50, float thickness = 1.0f)
{
	ImVec2* points = new ImVec2[num_segments + 1];

	float slice_angle = (end_angle - start_angle) / (float)num_segments;
	for (int i = 0; i <= num_segments; i++)
	{
		float angle = start_angle + slice_angle * (float)i;
		points[i].x = center.x + cosf(angle) * radius;
		points[i].y = center.y - sinf(angle) * radius;
	}

	draw_list->AddPolyline(points, num_segments + 1, color, false, thickness);
	delete[] points;
}

inline void BoardView::DrawArcs(ImDrawList *draw) {
	uint32_t cmask  = 0xFFFFFFFF;
	uint32_t omask  = 0x00000000;
	auto io         = ImGui::GetIO();

	if (pinSelectMasks) {
		if (m_pinSelected || m_pinHighlighted.size()) {
			cmask = m_colors.selectedMaskPins;
			omask = m_colors.orMaskPins;
		}
	}
	draw->ChannelsSetCurrent(kChannelPolylines);

	const auto& arcs = m_board->arcs();
	for (const auto &arc : arcs) {
		if (!(m_pinSelected && m_pinSelected->net == arc->net) && !(m_viaSelected && m_viaSelected->net == arc->net) && !BoardElementIsVisible(arc)) continue;
		ImVec2 pos = CoordToScreen(arc->position.x, arc->position.y);

		uint32_t color      = (m_colors.layerColor[arc->board_side][0] & cmask) | omask;
		auto radius = arc->radius * m_scale;
		if ((m_pinSelected && m_pinSelected->net == arc->net ) || (m_viaSelected && m_viaSelected->net == arc->net)) {
			DrawArc(draw, pos, radius, m_colors.defaultBoardSelectColor, arc->startAngle, arc->endAngle, 50, m_scale*1.5);
		}
		DrawArc(draw, pos, radius, color, arc->startAngle, arc->endAngle, 50, m_scale);
		//draw->AddText(pos, color, std::to_string(arc->startAngle * 180 / 3.1415).c_str());
		//draw->AddText(ImVec2(pos.x, pos.y - 10), color, std::to_string(arc->endAngle * 180 / 3.1415).c_str());
	}
}

static void DrawFilledSemiCircle(ImDrawList* draw_list, ImVec2 center, float radius, ImU32 color, bool right_half, int num_segments = 50)
{
	ImVec2* points = new ImVec2[num_segments + 2];
	points[0] = center;

	float start_angle = right_half ? 3.0f * M_PI / 2 : M_PI / 2;
	float end_angle   = right_half ? M_PI * 5 / 2 : 3.0f * M_PI / 2;

	for (int i = 0; i <= num_segments; i++)
	{
		float angle = start_angle + (end_angle - start_angle) * (float)i / (float)num_segments;
		points[i + 1].x = center.x + cosf(angle) * radius;
		points[i + 1].y = center.y + sinf(angle) * radius;
	}

	draw_list->AddConvexPolyFilled(points, num_segments + 2, color);
	delete[] points;
}
inline void BoardView::DrawVies(ImDrawList *draw) {
	uint32_t cmask  = 0xFFFFFFFF;
	uint32_t omask  = 0x00000000;
	auto io         = ImGui::GetIO();

	if (pinSelectMasks) {
		if (m_pinSelected || m_pinHighlighted.size()) {
			cmask = m_colors.selectedMaskPins;
			omask = m_colors.orMaskPins;
		}
	}
	draw->ChannelsSetCurrent(kChannelPolylines);

	const auto& vias = m_board->Vias();
	for (const auto &via : vias) {
		if (!(m_pinSelected && m_pinSelected->net == via->net) && !(m_viaSelected && m_viaSelected->net == via->net) && !BoardElementIsVisible(via)) continue;
		auto pos = CoordToScreen(via->position.x, via->position.y);
		auto radius = via->size * 0.5 * m_scale;
		if (!IsVisibleScreen(pos.x, pos.y, radius, io)) continue;

		uint32_t color      = (m_colors.viaColor & cmask) | omask;
		if ((m_pinSelected && m_pinSelected->net == via->net) || (m_viaSelected && m_viaSelected->net == via->net)) {
			color      = m_colors.pinSelectedColor;
		}
		draw->AddCircleFilled(pos, radius, color);
		if (radius > 3) {
			const auto offset = radius * 0.5;
			const auto leftPos = ImVec2(pos.x - offset, pos.y - offset);
			const auto rightPos = ImVec2(pos.x + 0.5f, pos.y - offset);
			auto text = std::to_string(via->board_side);
			auto text1 = std::to_string(via->target_side);
			ImFont *font = ImGui::GetIO().Fonts->Fonts[0]; // Default font
			ImVec2 text_size_normalized = font->CalcTextSizeA(1.0f, FLT_MAX, 0.0f, text.c_str());

			float maxfontwidth = radius * 1/ text_size_normalized.x; // Fit horizontally with 6.75% overflow (should still avoid colliding with neighbours)
			float maxfontheight = radius * 1/ text_size_normalized.y; // Fit vertically with 25% top/bottom padding
			float maxfontsize = min(maxfontwidth, maxfontheight);


			if (maxfontheight < font->FontSize * 0.75) {
				font = ImGui::GetIO().Fonts->Fonts[2]; // Use smaller font for pin name
			} else if (maxfontheight > font->FontSize * 1.5 && ImGui::GetIO().Fonts->Fonts[1]->FontSize > font->FontSize) {
				font = ImGui::GetIO().Fonts->Fonts[1]; // Use larger font for pin name
			}

			DrawFilledSemiCircle(draw, pos, radius*0.8, m_colors.layerColor[via->board_side][0], false);
			DrawFilledSemiCircle(draw, pos, radius*0.8, m_colors.layerColor[via->target_side][0], true);

			draw->ChannelsSetCurrent(kChannelText);
			draw->AddText(font, maxfontsize, leftPos, 0xFFFFFFFF, text.c_str());
			draw->AddText(font, maxfontsize, rightPos, 0xFFFFFFFF, text1.c_str());
			draw->ChannelsSetCurrent(kChannelPins);
		}
	}
}

void BoardView::DrawPartTooltips(ImDrawList *draw) {
	ImVec2 spos = ImGui::GetMousePos();
	ImVec2 pos  = ScreenToCoord(spos.x, spos.y);

	//	if (m_parent_occluded) return;
	if (!m_tooltips_enabled) return;
	if (spos.x > m_board_surface.x) return;
	/*
	 * I am loathing that I have to add this, but basically check every pin on the board so we can
	 * determine if we're hovering over a testpad
	 */
	for (auto &pin : m_board->Pins()) {

		if (pin->type == Pin::kPinTypeTestPad) {
			float dx   = pin->position.x - pos.x;
			float dy   = pin->position.y - pos.y;
			float dist = dx * dx + dy * dy;
			if ((dist < (pin->diameter * pin->diameter))) {
				float pd = pin->diameter * m_scale;

				draw->AddCircle(CoordToScreen(pin->position.x, pin->position.y), pd, m_colors.pinHaloColor, 32, pinHaloThickness);
				ImGui::PushStyleColor(ImGuiCol_Text, m_colors.annotationPopupTextColor);
				ImGui::PushStyleColor(ImGuiCol_PopupBg, m_colors.annotationPopupBackgroundColor);
				ImGui::BeginTooltip();
				ImGui::Text("TP[%s]%s", pin->name.c_str(), pin->net->name.c_str());
				ImGui::EndTooltip();
				ImGui::PopStyleColor(2);
				break;
			} // if in the required diameter
		}
	}

	currentlyHoveredPart = nullptr;
	for (auto &part : m_board->Components()) {
		if (part->component_type == Component::kComponentTypeBoard) continue;
		int hit = 0;
		//		auto p_part = part.get();


		// Work out if the point is inside the hull
		{
			auto poly = part->outline;

			for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
				if (((poly[i].y > pos.y) != (poly[j].y > pos.y)) &&
				    (pos.x < (poly[j].x - poly[i].x) * (pos.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x))
					hit ^= 1;
			}
		}

		// If we're inside a part
		if (hit) {
			currentlyHoveredPart = part;
			//			fprintf(stderr,"InPart: %s\n", currentlyHoveredPart->name.c_str());

			float min_dist = m_pinDiameter / 2.0f;
			min_dist *= min_dist; // all distance squared
			currentlyHoveredPin = nullptr;

			for (auto &pin : currentlyHoveredPart->pins) {
				// auto p     = pin;
				float dx   = pin->position.x - pos.x;
				float dy   = pin->position.y - pos.y;
				float dist = dx * dx + dy * dy;

				if (pin->net->is_ground || !BoardElementIsVisible(pin)) {
					continue;
				}

				if ((dist < (pin->diameter * pin->diameter)) && (dist < min_dist)) {
					currentlyHoveredPin = pin;
					//					fprintf(stderr,"Pinhit: %s\n",pin->number.c_str());
					min_dist = dist;
				} // if in the required diameter
			}     // for each pin in the part

			if (!BoardElementIsVisible(part) && (!currentlyHoveredPin || !BoardElementIsVisible(currentlyHoveredPin))) {
				continue;
			}

			if (part->outline_done) {

				/*
				 * Draw the bounding box for the part
				 */
				ImVec2 a, b, c, d;

				a = ImVec2(CoordToScreen(part->outline[0].x, part->outline[0].y));
				b = ImVec2(CoordToScreen(part->outline[1].x, part->outline[1].y));
				c = ImVec2(CoordToScreen(part->outline[2].x, part->outline[2].y));
				d = ImVec2(CoordToScreen(part->outline[3].x, part->outline[3].y));
				draw->AddQuad(a, b, c, d, m_colors.partHighlightedColor, 2);
			}

			draw->ChannelsSetCurrent(kChannelAnnotations);

			if (currentlyHoveredPin)
				draw->AddCircle(CoordToScreen(currentlyHoveredPin->position.x, currentlyHoveredPin->position.y),
				                currentlyHoveredPin->diameter * m_scale,
				                m_colors.pinHaloColor,
				                32,
				                pinHaloThickness);
			ImGui::PushStyleColor(ImGuiCol_Text, m_colors.annotationPopupTextColor);
			ImGui::PushStyleColor(ImGuiCol_PopupBg, m_colors.annotationPopupBackgroundColor);
			ImGui::BeginTooltip();
			if (currentlyHoveredPin) {
				ImGui::Text("%s\n[%s]%s",
				            currentlyHoveredPart->name.c_str(),
				            (currentlyHoveredPin ? currentlyHoveredPin->name.c_str() : " "),
				            (currentlyHoveredPin ? currentlyHoveredPin->net->name.c_str() : " "));
			} else {
				ImGui::Text("%s", currentlyHoveredPart->name.c_str());
			}
			ImGui::EndTooltip();
			ImGui::PopStyleColor(2);
		}

	} // for each part on the board
}

inline void BoardView::DrawPinTooltips(ImDrawList *draw) {
	if (!m_tooltips_enabled) return;
	draw->ChannelsSetCurrent(kChannelAnnotations);

	if (HighlightedPinIsHovered()) {
		ImGui::PushStyleColor(ImGuiCol_Text, m_colors.annotationPopupTextColor);
		ImGui::PushStyleColor(ImGuiCol_PopupBg, m_colors.annotationPopupBackgroundColor);
		ImGui::BeginTooltip();
		ImGui::Text("%s[%s]\n%s",
		            m_pinHighlightedHovered->component->name.c_str(),
		            m_pinHighlightedHovered->name.c_str(),
		            m_pinHighlightedHovered->net->name.c_str());
		ImGui::EndTooltip();
		ImGui::PopStyleColor(2);
	}
}

inline void BoardView::DrawAnnotations(ImDrawList *draw) {

	if (!showAnnotations) return;
	if (!m_tooltips_enabled) return;

	draw->ChannelsSetCurrent(kChannelAnnotations);

	for (auto &ann : m_annotations.annotations) {
		if (ann.side == m_current_side || (m_track_mode && m_current_side == kBoardSideTop)) {
			ImVec2 a, b, s;
			if (debug) fprintf(stderr, "%d:%d:%f %f: %s\n", ann.id, ann.side, ann.x, ann.y, ann.note.c_str());
			a = s = CoordToScreen(ann.x, ann.y);
			a.x += annotationBoxOffset;
			a.y -= annotationBoxOffset;
			b = ImVec2(a.x + annotationBoxSize, a.y - annotationBoxSize);

			if ((ann.hovered == true) && (m_tooltips_enabled)) {
				char buf[60];

				snprintf(buf, sizeof(buf), "%s", ann.note.c_str());
				buf[50] = '\0';

				ImGui::PushStyleColor(ImGuiCol_Text, m_colors.annotationPopupTextColor);
				ImGui::PushStyleColor(ImGuiCol_PopupBg, m_colors.annotationPopupBackgroundColor);
				ImGui::BeginTooltip();
				ImGui::Text("%c(%0.0f,%0.0f) %s %s%c%s%c\n%s%s",
				            m_current_side == kBoardSideBottom ? 'B' : 'T',
				            ann.x,
				            ann.y,
				            ann.net.c_str(),
				            ann.part.c_str(),
				            ann.part.size() && ann.pin.size() ? '[' : ' ',
				            ann.pin.c_str(),
				            ann.part.size() && ann.pin.size() ? ']' : ' ',
				            buf,
				            ann.note.size() > 50 ? "..." : "");

				ImGui::EndTooltip();
				ImGui::PopStyleColor(2);
			} else {
			}
			draw->AddCircleFilled(s, DPIF(2), m_colors.annotationStalkColor, 8);
			draw->AddRectFilled(a, b, m_colors.annotationBoxColor);
			draw->AddRect(a, b, m_colors.annotationStalkColor);
			draw->AddLine(s, a, m_colors.annotationStalkColor);
		}
	}
}

bool BoardView::HighlightedPinIsHovered(void) {
	ImVec2 mp  = ImGui::GetMousePos();
	ImVec2 mpc = ScreenToCoord(mp.x, mp.y); // it's faster to compute this once than convert all pins

	m_pinHighlightedHovered = nullptr;

	/*
	 * See if any of the pins listed in the m_pinHighlighted vector are hovered over
	 */
	for (auto &p : m_pinHighlighted) {
		ImVec2 a = ImVec2(p->position.x, p->position.y);
		double r = p->diameter / 2.0f;
		if ((mpc.x > a.x - r) && (mpc.x < a.x + r) && (mpc.y > a.y - r) && (mpc.y < a.y + r)) {
			m_pinHighlightedHovered = p;
			return true;
		}
	}

	/*
	 * See if any of the pins in the same network as the SELECTED pin (single) are hovered
	 */
	for (auto &p : m_board->Pins()) {
		//	auto p   = pin.get();
		double r = p->diameter / 2.0f * m_scale;
		if (m_pinSelected && p->net == m_pinSelected->net) {
			ImVec2 a = ImVec2(p->position.x, p->position.y);
			if ((mpc.x > a.x - r) && (mpc.x < a.x + r) && (mpc.y > a.y - r) && (mpc.y < a.y + r)) {
				m_pinHighlightedHovered = p;
				return true;
			}
		}
	}

	/*
	 * See if any pins of a highlighted part are hovered
	 */
	for (auto &part : m_partHighlighted) {
		for (auto &p : part->pins) {
			double r = p->diameter / 2.0f * m_scale;
			ImVec2 a = ImVec2(p->position.x, p->position.y);
			if ((mpc.x > a.x - r) && (mpc.x < a.x + r) && (mpc.y > a.y - r) && (mpc.y < a.y + r)) {
				m_pinHighlightedHovered = p;
				return true;
			}
		}
	}

	return false;
}

int BoardView::AnnotationIsHovered(void) {
	ImVec2 mp       = ImGui::GetMousePos();
	bool is_hovered = false;
	int i           = 0;

	if (!m_tooltips_enabled) return false;
	m_annotation_last_hovered = 0;

	for (auto &ann : m_annotations.annotations) {
		ImVec2 a = CoordToScreen(ann.x, ann.y);
		if ((mp.x > a.x + annotationBoxOffset) && (mp.x < a.x + (annotationBoxOffset + annotationBoxSize)) &&
		    (mp.y < a.y - annotationBoxOffset) && (mp.y > a.y - (annotationBoxOffset + annotationBoxSize))) {
			ann.hovered               = true;
			is_hovered                = true;
			m_annotation_last_hovered = i;
		} else {
			ann.hovered = false;
		}
		i++;
	}

	if (is_hovered == false) m_annotation_clicked_id = -1;

	return is_hovered;
}

/*
 * TODO
 * EXPERIMENTAL, draw an area around selected pins
 */
void BoardView::DrawSelectedPins(ImDrawList *draw) {
	std::vector<ImVec2> pl;
	if ((m_pinHighlighted.size()) < 3) return;
	for (auto &p : m_pinHighlighted) {
		pl.push_back(CoordToScreen(p->position.x, p->position.y));
	}
	std::vector<ImVec2> hull = VHConvexHull(pl);
	if (hull.size() > 3) {
		draw->AddConvexPolyFilled(hull.data(), hull.size(), 0x660000ff);
	}
}

void BoardView::DrawBoard() {
	if (!m_file || !m_board) return;

	ImDrawList *draw = ImGui::GetWindowDrawList();
	if (!m_needsRedraw) {
		memcpy(draw, m_cachedDrawList, sizeof(ImDrawList));
		memcpy(draw->CmdBuffer.Data, m_cachedDrawCommands.Data, m_cachedDrawCommands.Size);
		return;
	}

	// Splitting channels, drawing onto those and merging back.
	draw->ChannelsSplit(NUM_DRAW_CHANNELS);

	// We draw the Parts before the Pins so that we can ascertain the needed pin
	// size for the parts based on the part/pad geometry and spacing. -Inflex
	// OutlineGenerateFill();
	//	DrawFill(draw);
	DrawTracks(draw);
	DrawArcs(draw);
	DrawParts(draw);
	//	DrawSelectedPins(draw);
	DrawPins(draw);
	DrawVies(draw);
	OutlineGenFillDraw(draw, boardFillSpacing, 1);
	DrawOutline(draw);
	// DrawPinTooltips(draw);
	DrawPartTooltips(draw);
	DrawAnnotations(draw);

	draw->ChannelsMerge();

	// Copy the new draw list and cmd buffer:
	memcpy(m_cachedDrawList, draw, sizeof(ImDrawList));
	int cmds_size = draw->CmdBuffer.size() * sizeof(ImDrawCmd);
	m_cachedDrawCommands.resize(cmds_size);
	memcpy(m_cachedDrawCommands.Data, draw->CmdBuffer.Data, cmds_size);
	m_needsRedraw = false;
}
/** end of drawing region **/

int qsort_netstrings(const void *a, const void *b) {
	const char *sa = *(const char **)a;
	const char *sb = *(const char **)b;
	return strcmp(sa, sb);
}

/**
 * CenterView
 *
 * Resets the scale and transformation back to original.
 * Does NOT change the rotation (yet?)
 *
 * PLD20160621-1715
 *
 */
void BoardView::CenterView(void) {
	// ImVec2 view = ImGui::GetIO().DisplaySize;
	ImVec2 view = m_board_surface;

	float dx = 1.1f * (m_boardWidth);
	float dy = 1.1f * (m_boardHeight);
	float sx = dx > 0 ? view.x / dx : 1.0f;
	float sy = dy > 0 ? view.y / dy : 1.0f;

	//  m_rotation = 0;
	m_scale_floor = m_scale = sx < sy ? sx : sy;
	SetTarget(m_mx, m_my);
	m_needsRedraw = true;
}

void BoardView::LoadBoard(BRDFileBase *file) {
	delete m_board;

	// Check board outline (format) point count.
	//		If we don't have an outline, generate one
	//
	if (file->outline_segments.size() < 3 && file->format.size() < 3) {
		auto pins = file->pins;
		int minx, maxx, miny, maxy;
		int margin = 200; // #define or leave this be? Rather arbritary.

		minx = miny = INT_MAX;
		maxx = maxy = INT_MIN;

		for (auto a : pins) {
			if (a.pos.x > maxx) maxx = a.pos.x;
			if (a.pos.y > maxy) maxy = a.pos.y;
			if (a.pos.x < minx) minx = a.pos.x;
			if (a.pos.y < miny) miny = a.pos.y;
		}

		maxx += margin;
		maxy += margin;
		minx -= margin;
		miny -= margin;

		file->format.push_back({minx, miny});
		file->format.push_back({maxx, miny});
		file->format.push_back({maxx, maxy});
		file->format.push_back({minx, maxy});
		file->format.push_back({minx, miny});
	}

	m_board = new BRDBoard(file);
	searcher.setParts(m_board->Components());
	searcher.setNets(m_board->Nets());

	std::vector<std::string> netnames;
	for (auto &n : m_board->Nets()) netnames.push_back(n->name);
	std::vector<std::string> partnames;
	for (auto &p : m_board->Components()) netnames.push_back(p->name);

	scnets.setDictionary(netnames);
	scparts.setDictionary(partnames);

	m_nets = m_board->Nets();

	int min_x = INT_MAX, max_x = INT_MIN, min_y = INT_MAX, max_y = INT_MIN;
	for (auto &pa : m_board->OutlinePoints()) {
		if (pa->x < min_x) min_x = pa->x;
		if (pa->y < min_y) min_y = pa->y;
		if (pa->x > max_x) max_x = pa->x;
		if (pa->y > max_y) max_y = pa->y;
	}
	for (auto &s: m_board->OutlineSegments()) {
		if (s.first.x < min_x) min_x = s.first.x;
		if (s.second.x < min_x) min_x = s.second.x;
		if (s.first.y < min_y) min_y = s.first.y;
		if (s.second.y < min_y) min_y = s.second.y;
		if (s.first.x > max_x) max_x = s.first.x;
		if (s.second.x > max_x) max_x = s.second.x;
		if (s.first.y > max_y) max_y = s.first.y;
		if (s.second.y > max_y) max_y = s.second.y;
	}

	// ImVec2 view = ImGui::GetIO().DisplaySize;
	ImVec2 view = m_board_surface;

	m_mx = (float)(min_x + max_x) / 2.0f;
	m_my = (float)(min_y + max_y) / 2.0f;

	float dx = 1.1f * (max_x - min_x);
	float dy = 1.1f * (max_y - min_y);
	float sx = dx > 0 ? view.x / dx : 1.0f;
	float sy = dy > 0 ? view.y / dy : 1.0f;

	m_scale_floor = m_scale = sx < sy ? sx : sy;
	m_boardWidth            = max_x - min_x;
	m_boardHeight           = max_y - min_y;
	SetTarget(m_mx, m_my);

	m_pinHighlighted.reserve(m_board->Components().size());
	m_partHighlighted.reserve(m_board->Components().size());
	m_pinSelected = nullptr;

	m_firstFrame  = true;
	m_needsRedraw = true;

	m_track_mode = !m_board->Tracks().empty();
}

ImVec2 BoardView::CoordToScreen(float x, float y, float w) {
	float side = m_current_side == kBoardSideBottom ? -1.0f : 1.0f;
	if (m_track_mode)
		side = 1.0f;
	float tx   = side * m_scale * (x + w * (m_dx - m_mx));
	float ty   = -1.0f * m_scale * (y + w * (m_dy - m_my));
	switch (m_rotation) {
		case 0: return ImVec2(tx, ty);
		case 1: return ImVec2(-ty, tx);
		case 2: return ImVec2(-tx, -ty);
		default: return ImVec2(ty, -tx);
	}
}

ImVec2 BoardView::ScreenToCoord(float x, float y, float w) {
	float tx, ty;

	switch (m_rotation) {
		case 0:
			tx = x;
			ty = y;
			break;
		case 1:
			tx = y;
			ty = -x;
			break;
		case 2:
			tx = -x;
			ty = -y;
			break;
		default:
			tx = -y;
			ty = x;
			break;
	}
	float side     = m_current_side == kBoardSideBottom ? -1.0f : 1.0f;
	if (m_track_mode)
		side = 1.0f;
	float invscale = 1.0f / m_scale;

	tx = tx * side * invscale + w * (m_mx - m_dx);
	ty = ty * -1.0f * invscale + w * (m_my - m_dy);

	return ImVec2(tx, ty);
}

void BoardView::Rotate(int count) {
	// too lazy to do math
	while (count > 0) {
		m_rotation = (m_rotation + 1) & 3;
		float dx   = m_dx;
		float dy   = m_dy;
		if (m_current_side == kBoardSideTop) {
			m_dx = -dy;
			m_dy = dx;
		} else {
			m_dx = dy;
			m_dy = -dx;
		}
		if (m_track_mode) {
			m_dx = -dy;
			m_dy = dx;
		}
		--count;
		m_needsRedraw = true;
	}
	while (count < 0) {
		m_rotation = (m_rotation - 1) & 3;
		float dx   = m_dx;
		float dy   = m_dy;
		if (m_current_side == kBoardSideBottom) {
			m_dx = -dy;
			m_dy = dx;
		} else {
			m_dx = dy;
			m_dy = -dx;
		}
		if (m_track_mode) {
			m_dx = dy;
			m_dy = -dx;
		}
		++count;
		m_needsRedraw = true;
	}
}

void BoardView::Mirror(void) {
	auto &outline = m_board->OutlinePoints();
	ImVec2 min, max;

	// find the orthagonal bounding box
	// probably can put this as a predefined
	min.x = min.y = FLT_MAX;
	max.x = max.y = FLT_MIN;
	for (auto &p : outline) {
		if (p->x < min.x) min.x = p->x;
		if (p->y < min.y) min.y = p->y;
		if (p->x > max.x) max.x = p->x;
		if (p->y > max.y) max.y = p->y;
	}

	for (auto &p : outline) {
		p->x = max.x - p->x;
	}

	for (auto &p : m_board->Pins()) {
		//	auto p        = pin.get();
		p->position.x = max.x - p->position.x;
	}

	for (auto &part : m_board->Components()) {

		part->centerpoint.x = max.x - part->centerpoint.x;

		if (part->outline_done) {
			for (size_t i = 0; i < part->outline.size(); i++) {
				part->outline[i].x = max.x - part->outline[i].x;
			}
		}

		for (size_t i = 0; i < part->hull.size(); i++) {
			part->hull[i].x = max.x - part->hull[i].x;
		}
	}

	for (auto &ann : m_annotations.annotations) {
		ann.x = max.x - ann.x;
	}
}

void BoardView::SetTarget(float x, float y) {
	// ImVec2 view  = ImGui::GetIO().DisplaySize;
	ImVec2 view  = m_board_surface;
	ImVec2 coord = ScreenToCoord(view.x / 2.0f, view.y / 2.0f);
	m_dx += coord.x - x;
	m_dy += coord.y - y;
}

inline bool BoardView::BoardElementIsVisible(const std::shared_ptr<BoardElement> be) {
	if (!be) return true; // no element? => no board side info

	if (be->board_side == m_current_side) return true;

	if (m_track_mode) {
		const auto sz = m_board->AllSide().size();
		if (sz + 1 - be->board_side == m_current_side)
			return true;
	}

	if (be->board_side == kBoardSideBoth) return true;

	if (auto via = dynamic_pointer_cast<Via>(be); via != nullptr) {
		auto minLayer = std::min(via->board_side, via->target_side);
		auto maxLayer = std::max(via->board_side, via->target_side);
		if (m_current_side >= minLayer && m_current_side <= maxLayer) return true;
		if (m_track_mode) {
			const auto sz = m_board->AllSide().size();
			auto minLayer1 = EBoardSide(sz + 1 - maxLayer);
			auto maxLayer1 = EBoardSide(sz + 1 - minLayer);
			if (m_current_side >= minLayer1 && m_current_side <= maxLayer1) return true;
		}
	}

	return false;
}

inline bool BoardView::IsVisibleScreen(float x, float y, float radius, const ImGuiIO &io) {
	// if (x < -radius || y < -radius || x - radius > io.DisplaySize.x || y - radius > io.DisplaySize.y) return false;
	if (x < -radius || y < -radius || x - radius > m_board_surface.x || y - radius > m_board_surface.y) return false;
	return true;
}

bool BoardView::PartIsHighlighted(const std::shared_ptr<Component> component) {
	bool highlighted = contains(component, m_partHighlighted);

	// is any pin of this part selected?
	if (m_pinSelected) highlighted |= m_pinSelected->component == component;

	return highlighted;
}

bool BoardView::AnyItemVisible(void) {
	bool any_visible = false;

	if (m_searchComponents) {
		for (auto &p : m_partHighlighted) {
			any_visible |= BoardElementIsVisible(p);
		}
	}

	if (!any_visible) {
		if (m_searchNets) {
			for (auto &p : m_pinHighlighted) {
				any_visible |= BoardElementIsVisible(p->component);
			}
		}
	}

	return any_visible;
}

void BoardView::FindNetNoClear(const char *name) {
	if (!m_file || !m_board || !(*name)) return;

	auto results = searcher.nets(name);

	for (auto &net : results) {
		for (auto &pin : net->pins) m_pinHighlighted.push_back(pin);
	}
}

void BoardView::FindNet(const char *name) {
	m_pinHighlighted.clear();
	FindNetNoClear(name);
}

void BoardView::FindComponentNoClear(const char *name) {
	if (!m_file || !m_board || !name) return;

	auto results = searcher.parts(name);

	for (auto &p : results) {
		m_partHighlighted.push_back(p);

		for (auto &pin : p->pins) {
			m_pinHighlighted.push_back(pin);
		}
	}
	m_needsRedraw = true;
}

void BoardView::FindComponent(const char *name) {
	if (!m_file || !m_board) return;

	m_pinHighlighted.clear();
	m_partHighlighted.clear();

	FindComponentNoClear(name);
}

void BoardView::SearchCompoundNoClear(const char *item) {
	if (*item == '\0') return;
	if (debug) fprintf(stderr, "Searching for '%s'\n", item);
	if (m_searchComponents) FindComponentNoClear(item);
	if (m_searchNets) FindNetNoClear(item);
	if (!m_partHighlighted.empty() && !m_pinHighlighted.empty() && !AnyItemVisible())
		FlipBoard(1); // passing 1 to override flipBoard parameter
}

void BoardView::SearchCompound(const char *item) {
	m_pinHighlighted.clear();
	m_partHighlighted.clear();
	//	ClearAllHighlights();
	if (*item == '\0') return;

	SearchCompoundNoClear(item);
}

void BoardView::SetLastFileOpenName(const std::string &name) {
	m_lastFileOpenName = name;
}

void BoardView::FlipBoard(int mode) {
	ImVec2 mpos = ImGui::GetMousePos();
	// ImVec2 view = ImGui::GetIO().DisplaySize;
	ImVec2 view = m_board_surface;
	ImVec2 bpos = ScreenToCoord(mpos.x, mpos.y);
	auto io     = ImGui::GetIO();

	if (mode == 1)
		mode = 0;
	else
		mode = flipMode;

 	if (m_track_mode) {
		const auto &all_side = m_board->AllSide();
		// skip empty side
		auto iter = std::find(all_side.cbegin(), all_side.cend(), m_current_side);
		if (iter != all_side.cend()) {
			++iter;
		}
		m_current_side = iter == all_side.cend() ? kBoardSideTop : *iter;
		if (m_current_side == kBoardSideBottom)
			m_current_side = kBoardSideTop;
		else if (m_current_side == *(all_side.data() + all_side.size()/2))
			m_current_side = EBoardSide(m_current_side + 1);
		m_needsRedraw = true;
		return;
	}

	m_current_side = m_current_side == kBoardSideTop ? kBoardSideBottom : kBoardSideTop;

	m_dx           = -m_dx;

	if (m_flipVertically) {
		Rotate(2);
		if (io.KeyShift ^ mode) {
			SetTarget(bpos.x, bpos.y);
			Pan(DIR_RIGHT, view.x / 2 - mpos.x);
			Pan(DIR_DOWN, view.y / 2 - mpos.y);
		}
	}
	m_needsRedraw = true;
}

void BoardView::HandlePDFBridgeSelection() {
	if (pdfBridge.HasNewSelection()) {
		auto selection = pdfBridge.GetSelection();
		if (selection.empty()) {
			m_pinHighlighted.clear();
			m_partHighlighted.clear();
		} else {
			SearchCompound(selection.c_str());
			CenterZoomSearchResults();
		}
	};
}

BitVec::~BitVec() {
	free(m_bits);
}

void BitVec::Resize(uint32_t new_size) {
	if (new_size > m_size) {
		uint32_t bytelen     = 4 * ((m_size + 31) / 32);
		uint32_t new_bytelen = 4 * ((new_size + 31) / 32);
		uint32_t *new_bits   = (uint32_t *)malloc(new_bytelen);
		if (m_bits) {
			memcpy(new_bits, m_bits, bytelen);
			free(m_bits);
			memset((char *)new_bits + bytelen, 0, new_bytelen - bytelen);
		} else {
			memset(new_bits, 0, new_bytelen);
		}
		m_bits = new_bits;
	}
	m_size = new_size;
}
