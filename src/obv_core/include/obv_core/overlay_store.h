#pragma once

#include "obv_core/filesystem_impl.h"

#include <map>
#include <string>
#include <vector>

// annotations.h uses std::string/std::vector without including them; pull them first.
#include "annotations.h"
namespace obv {

// Serializable freeform annotation (SQLite-backed when HAVE_SQLITE3).
// `visible` is always true for rows loaded into memory (desktop filters visible=1).
struct OverlayAnnotation {
	int id = 0;
	int side = 0;
	double x = 0;
	double y = 0;
	std::string net;
	std::string part;
	std::string pin;
	std::string note;
	bool visible = true;
};

// Combined overlay document for the HTTP API (design section 5.3).
// On disk: boardPath.yaml (Version 0.0.2 PartInfos/NetInfos) + optional boardPath-derived .sqlite3.
struct OverlayDocument {
	std::vector<OverlayAnnotation> annotations;
	std::map<std::string, PartInfo> partInfos;
	std::map<std::string, NetInfo> netInfos;
};

// Load freeform annotations (SQLite when HAVE_SQLITE3) and YAML PartInfos/NetInfos for boardPath.
// Desktop naming: yaml = boardPath.string() + ".yaml"; sqlite replaces last '.' with '_' then + ".sqlite3".
bool LoadOverlayForBoard(const filesystem::path &boardPath, Annotations &out, std::string &err);

// Persist PartInfos/NetInfos to boardPath.yaml (Version 0.0.2) via Annotations::SavePinInfos.
// SavePinInfos cannot signal write failure; success is verified by create/mtime/size change + Version re-read.
bool SavePartNetYaml(const filesystem::path &boardPath, const Annotations &ann, std::string &err);

// Freeform annotations: use Annotations::Add/Update/Remove then GenerateList after SetFilename/Load.
// JSON export of in-memory annotations + partInfos + netInfos.
std::string ExportOverlayJson(const Annotations &ann);

// PUT overlays: replace partInfos/netInfos from JSON. Annotation fields in JSON are ignored
// (annotation CRUD uses discrete Add/Update/Remove matching desktop SQL).
bool ApplyOverlayJson(Annotations &ann, const std::string &json, std::string &err);

} // namespace obv
