#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <cstdint>
#include <string>

#include "confparse.h"

class Config {
public:
	int windowX = 1200;
	int windowY = 700;
	int dpi = 100;
	double fontSize              = 20.0;
	std::string fontName         = "";
	float zoomFactor             = 0.5f;
	float partZoomScaleOutFactor = 3.0f;
	int zoomModifier             = 5;
	int panFactor                = 30;
	int panModifier              = 5;
	int flipMode                 = 0;

	int annotationBoxOffset = 8;
	int annotationBoxSize   = 20;

	int pinA1threshold = 3; // pincount of package to show 1/A1 pin
	int netWebThickness = 2;

	float pinSizeThresholdLow = 0.0f;
	bool pinShapeSquare       = false;
	bool pinShapeCircle       = true;
	bool pinSelectMasks       = true;
	bool slowCPU              = false;
	bool showFPS              = false;
	bool showNetWeb           = true;
	bool showInfoPanel        = true;
	int infoPanelWidth        = 300;
	bool showPins             = true;
	bool showAnnotations      = true;
	bool showBackgroundImage  = true;
	bool pinHalo              = false;
	float pinHaloDiameter     = 1.1;
	float pinHaloThickness    = 4.00;
	bool fillParts            = true;
	bool boardFill            = true;
	bool showPartName         = true;
	bool showPinName          = true;
	bool showPartType     	  = true;
	enum ShowMode : int {
		ShowMode_None, ShowMode_Diode, ShowMode_Voltage, ShowMode_Ohm
	};
	ShowMode showMode = ShowMode::ShowMode_Diode;
	int boardFillSpacing      = 3;
	bool showPosition  = true;

	bool infoPanelCenterZoomNets   = true;
	bool infoPanelSelectPartsOnNet = true;
	bool infoPanelSelectPartsOnNetOnlyNotGround = false;
	bool centerZoomSearchResults = true;

#ifdef _WIN32
	std::string pdfSoftwarePath;
#endif

	std::string FZKeyStr = "";
	uint32_t FZKey[44] = {0};

	void SetFZKey(const char *keytext);

	void readFromConfig(Confparse &obvconfig);
	void writeToConfig(Confparse &obvconfig);
};

#endif//_CONFIG_H_
