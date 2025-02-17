#include "BRDFileBase.h"

#include "utf8/utf8.h"
#include <cstdint>
#include <cmath>

double BRDFileBase::arc_slice_angle_rad = 0.1;

// from stb.h
void stringfile(char *buffer, std::vector<char*> &lines) {
	char *s;
	size_t count, i;

	// two passes through: first time count lines, second time set them
	for (i = 0; i < 2; ++i) {
		s                   = buffer;
		if (i == 1) lines[0] = s;
		count               = 1; // was '1',  but C arrays are 0-indexed
		while (*s) {
			if (*s == '\n' || *s == '\r') {

				// If this is the 2nd pass, then terminate the line at the first line break char
				if (i == 1) *s = 0;

				s++; // next char

				// if the termination is a CRLF combo, then jump to the next char
				if ((*s == '\r') || (*s == '\n')) s++;

				// if the char is valid (first after line break), set up the next item in the line array
				if (*s) { // it's not over yet
					if (i == 1) {
						lines[count] = s;
						//					  list[count+1] = NULL;
						// fprintf(stdout,"%s\n",list[count]);
					}
					++count;
				}
			}
			s++;
		} // while s

		// Generate the required array to hold all the line starting points
		if (i == 0) {
			lines.resize(count);
		}
	}
}

char *fix_to_utf8(char *s, char **arena, char *arena_end) {
	if (!utf8valid(s)) {
		return s;
	}
	char *p     = *arena;
	char *begin = p;
	while (*s) {
		uint32_t c = (uint8_t)*s;
		if (c < 0x80) {
			if (p + 1 >= arena_end) goto done;
			*p++ = c;
		} else {
			if (p + 2 >= arena_end) goto done;
			*p++ = 0xc0 | (c >> 6);
			*p++ = 0x80 | (c & 0x3f);
		}
		++s;
	}
	if (p + 1 >= arena_end) goto done;
	*p++ = 0;
done:
	*arena = p;
	return begin;
}

void BRDFileBase::AddNailsAsPins() {
	for (auto &nail : nails) {
		BRDPin pin;
		pin.pos = nail.pos;
		if (nail.side == BRDPartMountingSide::Both) {
			pin.part = parts.size();
			pin.side = BRDPinSide::Both;
		} else if (nail.side == BRDPartMountingSide::Top) {
			pin.part = parts.size();
			pin.side = BRDPinSide::Top;
		} else {
			pin.part  = parts.size() - 1;
			pin.side = BRDPinSide::Bottom;
		}
		pin.probe = nail.probe;
		pin.net   = nail.net;
		pins.push_back(pin);
	}
}

std::vector<std::pair<BRDPoint, BRDPoint> > BRDFileBase::arc_to_segments(
	double startAngle, double endAngle, double radius, BRDPoint pc)
{
	std::vector<std::pair<BRDPoint, BRDPoint>> arc_segments{};
	BRDPoint p1,p2;
	p1.x = pc.x + radius * cos(startAngle);
	p1.y = pc.y + radius * sin(startAngle);

	p2.x          = pc.x + radius * cos(endAngle);
	p2.y          = pc.y + radius * sin(endAngle);
	BRDPoint p = p1;
	BRDPoint pold = p1;
	for (double i = startAngle + arc_slice_angle_rad; i < endAngle; i += arc_slice_angle_rad) {
		p.x = pc.x + radius * cos(i);
		p.y = pc.y + radius * sin(i);
		arc_segments.push_back({pold, p});
		pold = p;
	}
	arc_segments.push_back({p, p2});

	return arc_segments;
}


double BRDFileBase::distance(const BRDPoint &p1, const BRDPoint &p2) {
	return sqrt(pow(p1.x - p2.x, 2) + pow(p1.y - p2.y, 2));
}
