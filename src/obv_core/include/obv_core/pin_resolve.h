#pragma once

#include "Board.h"
#include "annotations.h"

#include <string>

namespace obv {

// Prefer name, else number, else UniqueId() - matches web pinOverlayKey:
//   pin.name || pin.number || pin.id
// For domain Pin without export id: name || number || UniqueId()
std::string PinOverlayKey(const Pin &pin);

// Export pin id matching board_json.cpp pinId() (component.number or nail.number.index).
std::string ExportPinId(const Pin &pin, size_t globalIndex);

enum class MeasureSource { None, Overlay, Board, Propagated };

struct MeasureField {
	std::string board;
	std::string overlay;
	std::string localValue;
	MeasureSource localSource = MeasureSource::None;
	std::string effectiveValue;
	MeasureSource effectiveSource = MeasureSource::None;
	std::string fromComponent; // set when Propagated
	std::string fromPinKey;
	std::string fromPinId;
};

struct PinResolveResult {
	const Pin *pin = nullptr; // non-owning, points into snapshot board
	std::string pinKey;
	std::string netName;
	// Sequential export id matching board_json ExportBoardJson (not Net::number).
	// 0 means no net / null in JSON.
	int netId = 0;
	MeasureField diode, voltage, ohm, ohm_black;
	// overlay pin meta
	std::string overlayNote;
	std::string overlayShowName;
	PinVoltageFlag overlayVoltageFlag = PinVoltageFlag::unknown;
};

// Testable pure API: overlay > board > propagated (when local empty).
MeasureField ResolveOneField(const std::string &overlayVal,
                             const std::string &boardVal,
                             const std::string &propagatedVal,
                             const std::string &propComponent,
                             const std::string &propPinKey,
                             const std::string &propPinId);

// Find pin under part; matching order per spec section 4.2
const Pin *FindPartPin(const Board &board, const std::string &part, const std::string &pinRef);

// Fill measurements for a found pin
void ResolvePinMeasurements(const Board &board, const Annotations &ann,
                            const Pin &pin, PinResolveResult &out);

// High-level: find + resolve; returns false if part/pin missing
bool ResolvePartPin(const Board &board, const Annotations &ann,
                    const std::string &part, const std::string &pinRef,
                    PinResolveResult &out, std::string &errCode);
// errCode: PART_NOT_FOUND | PIN_NOT_FOUND

// JSON string for GET pin response (boardId/sourceName filled by caller or params)
std::string ExportPinResolveJson(const std::string &boardId,
                                 const std::string &sourceName,
                                 const std::string &part,
                                 const PinResolveResult &r);

// JSON string for GET part summary (board geometry + partInfo overlay)
std::string ExportPartSummaryJson(const Board &board, const Annotations &ann,
                                  const std::string &boardId,
                                  const std::string &sourceName,
                                  const std::string &part);
// Returns empty when part not found

// Part existence
const Component *FindComponent(const Board &board, const std::string &part);

// Generate condition id: next free oc_NNNN within the part's existing ids
std::string AllocateConditionId(const PartInfo &part);

// Validate/normalize condition fields; returns false + err message
bool NormalizeOperatingCondition(OperatingCondition &oc, std::string &err);
// Rules: id<=64, name/note<=2048, label<=128, arrays<=256, trim, drop empty labels

} // namespace obv
