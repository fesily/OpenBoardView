#pragma once

#include "Board.h"

#include <memory>
#include <cstring>
#include <vector>

class BRDBoard : public Board {
  public:
	BRDBoard(const BRDFileBase *const boardFile);
	~BRDBoard();

	const BRDFileBase *m_file;

	EBoardType BoardType();

	SharedVector<Net> &Nets();
	SharedVector<Component> &Components();
	SharedVector<Pin> &Pins();
	SharedVector<Point> &OutlinePoints();
	SharedVector<Track> &Tracks();
	SharedVector<Via> &Vias();
	SharedVector<PcbArc> &arcs();
	std::vector<std::pair<Point, Point>> &OutlineSegments();
	std::vector<EBoardSide> &AllSide();


  private:
	static const std::string kNetUnconnectedPrefix;
	static const std::string kComponentDummyName;

	SharedVector<Net> nets_;
	SharedVector<Component> components_;
	SharedVector<Pin> pins_;
	SharedVector<Point> outline_points_;
	SharedVector<Track> tracks_;
	SharedVector<Via> vias_;
	SharedVector<PcbArc> arcs_;
	std::vector<EBoardSide> all_side_;
	std::vector<std::pair<Point, Point>> outline_segments_;
};
