#include "NetList.h"

#include "imgui/imgui.h"
#include <string_view>
NetList::NetList(TcharStringCallback cbNetSelected) {
	cbNetSelected_ = cbNetSelected;
}

NetList::~NetList() {}

void NetList::Draw(const char *title, bool *p_open, Board *board) {
	// TODO: export / fix dimensions & behaviour
	int width  = 400;
	int height = 640;

	ImGui::SetNextWindowSize(ImVec2(width, height));
	ImGui::Begin("Net List");

	ImGui::Columns(1, "part_infos");
	ImGui::Separator();

	ImGui::Text("Name");
	ImGui::NextColumn();
	ImGui::Separator();

	if (board) {
		auto nets = board->Nets();

		static int selected = -1;
		string_view net_name     = "";
		ImGuiListClipper clipper;
		clipper.Begin(nets.size());
		while (clipper.Step()) {
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
				auto net = nets.at(i).get();
				net_name = net->name;
				if (ImGui::Selectable(
					net_name.data(), selected == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
					selected = i;
					if (ImGui::IsMouseDoubleClicked(0)) {
						cbNetSelected_(net_name.data());
					}
				}
				ImGui::NextColumn();
				net_name = net->show_name;
				if (net_name == net->name) continue;
				if (ImGui::Selectable(
					net_name.data(), selected == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
					selected = i;
					if (ImGui::IsMouseDoubleClicked(0)) {
						cbNetSelected_(net->name.c_str());
					}
				}
				ImGui::NextColumn();
			}
		}
		clipper.End();
	}
	ImGui::Columns(1);
	ImGui::Separator();

	ImGui::End();
}
