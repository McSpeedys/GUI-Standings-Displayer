// League of Legends Intramural Standings
// Dear ImGui + GLFW + OpenGL3. Data auto-saves to league_data.txt next to where you run it.

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

static const char* kSaveFile = "league_data.txt";

// ---------------------------------------------------------------- data model

// One best-of-3 series. Winner has 2 games, loser has 0 or 1.
struct Match {
    std::string a, b;
    int gamesA = 0, gamesB = 0;
    bool AWon() const { return gamesA > gamesB; }
    const std::string& Winner() const { return AWon() ? a : b; }
    const std::string& Loser()  const { return AWon() ? b : a; }
    int WinnerGames() const { return AWon() ? gamesA : gamesB; }
    int LoserGames()  const { return AWon() ? gamesB : gamesA; }
    bool Involves(const std::string& t) const { return a == t || b == t; }
};

struct Row {
    std::string name;
    int wins = 0, losses = 0;          // series
    int gamesWon = 0, gamesLost = 0;   // individual games
    char streakType = 0;               // 'W' or 'L'
    int streakLen = 0;
    bool wonOnH2H = false;             // ranked above a tied team via head-to-head
    int remaining = 0;                 // regular-season series left to play
    int status = 0;                    // +1 clinched playoffs, -1 eliminated, 0 undecided
    int GameDiff() const { return gamesWon - gamesLost; }
};

struct League {
    std::string title = "LoL Intramural Tournament";
    int playoffSpots = 4;
    int seriesPerTeam = 8;             // regular-season series each team plays
    std::vector<std::string> teams;
    std::vector<Match> matches;

    bool HasTeam(const std::string& n) const {
        return std::find(teams.begin(), teams.end(), n) != teams.end();
    }
    int Played(const std::string& n) const {
        int c = 0;
        for (auto& m : matches) if (m.Involves(n)) ++c;
        return c;
    }
    bool TeamHasMatches(const std::string& n) const {
        for (auto& m : matches) if (m.Involves(n)) return true;
        return false;
    }

    // Ranking: series wins, then fewest series losses. Teams still tied are
    // ordered by a head-to-head mini-table among just those teams (series wins,
    // then game diff in those series), then overall game diff, then games won.
    std::vector<Row> Standings() const {
        std::map<std::string, Row> rows;
        for (auto& t : teams) rows[t].name = t;
        for (auto& m : matches) {
            auto apply = [&](const std::string& team, bool won, int gw, int gl) {
                auto it = rows.find(team);
                if (it == rows.end()) return;
                Row& r = it->second;
                (won ? r.wins : r.losses)++;
                r.gamesWon += gw; r.gamesLost += gl;
                char c = won ? 'W' : 'L';
                if (r.streakType == c) r.streakLen++;
                else { r.streakType = c; r.streakLen = 1; }
            };
            apply(m.Winner(), true,  m.WinnerGames(), m.LoserGames());
            apply(m.Loser(),  false, m.LoserGames(),  m.WinnerGames());
        }
        std::vector<Row> out;
        for (auto& kv : rows) out.push_back(kv.second);
        std::sort(out.begin(), out.end(), [](const Row& a, const Row& b) {
            if (a.wins != b.wins) return a.wins > b.wins;
            return a.losses < b.losses;
        });

        // Break ties within each group of equal records.
        for (size_t i = 0; i < out.size();) {
            size_t j = i + 1;
            while (j < out.size() && out[j].wins == out[i].wins && out[j].losses == out[i].losses) ++j;
            if (j - i > 1) BreakTie(out, i, j);
            i = j;
        }
        ComputeClinch(out);
        return out;
    }

    // Marks teams that are mathematically in (+1) or out (-1) of the top
    // `playoffSpots`.
    //
    // The schedule of remaining series isn't known, but two facts are:
    //   - team i has r_i series left, and
    //   - every series produces exactly one win, so the remaining series hand
    //     out exactly R = (sum of r_i) / 2 wins in total.
    // Any split of those R wins with 0 <= x_i <= r_i is achievable by *some*
    // pairing of opponents (as long as no team has more series left than R,
    // which always holds for a consistent schedule). So a team is colored only
    // if the outcome holds for every such split. Ties are always assumed to go
    // the "wrong" way, so colors are never shown on a tiebreaker guess.
    void ComputeClinch(std::vector<Row>& out) const {
        bool seasonOver = !out.empty() && !matches.empty();
        int totalLeft = 0;
        for (auto& r : out) {
            r.remaining = std::max(0, seriesPerTeam - (r.wins + r.losses));
            totalLeft += r.remaining;
            if (r.remaining > 0) seasonOver = false;
        }
        if (playoffSpots <= 0) return;

        if (seasonOver) {   // final standings: the table order (incl. tiebreakers) decides
            for (size_t i = 0; i < out.size(); ++i) out[i].status = (int)i < playoffSpots ? 1 : -1;
            return;
        }

        // The win-total argument needs a consistent schedule (e.g. teams added
        // mid-season or an odd total can break it). If it doesn't hold, use the
        // simpler per-team check, which is weaker but always safe.
        int R = totalLeft / 2;
        bool consistent = totalLeft % 2 == 0;
        for (auto& r : out) if (r.remaining > R) consistent = false;

        for (auto& t : out) {
            if (consistent) {
                if (CannotBeCaught(out, t, R))      t.status = 1;
                else if (CannotQualify(out, t, R))  t.status = -1;
            } else {
                int couldFinishAhead = 0, alreadyAhead = 0;
                for (auto& u : out) {
                    if (&u == &t) continue;
                    if (u.wins + u.remaining >= t.wins) ++couldFinishAhead;
                    if (u.wins > t.wins + t.remaining) ++alreadyAhead;
                }
                if (couldFinishAhead < playoffSpots)   t.status = 1;
                else if (alreadyAhead >= playoffSpots) t.status = -1;
            }
        }
    }

    // Worst case for t: it loses everything left (finishes on t.wins) and the R
    // remaining wins go to other teams. t is clinched unless `playoffSpots`
    // other teams can all reach at least t.wins using at most R wins combined.
    bool CannotBeCaught(const std::vector<Row>& out, const Row& t, int R) const {
        std::vector<int> needs;
        for (auto& u : out) {
            if (&u == &t) continue;
            int need = std::max(0, t.wins - u.wins);
            if (need <= u.remaining) needs.push_back(need);
        }
        if ((int)needs.size() < playoffSpots) return true;
        std::sort(needs.begin(), needs.end());
        int cheapest = 0;
        for (int k = 0; k < playoffSpots; ++k) cheapest += needs[k];
        return cheapest > R;
    }

    // Best case for t: it wins everything left (finishes on T) and the other
    // R - t.remaining wins are spread to keep as many teams as possible at or
    // below T. t is eliminated if, even then, `playoffSpots` teams end above T.
    bool CannotQualify(const std::vector<Row>& out, const Row& t, int R) const {
        int T = t.wins + t.remaining;
        int toPlace = R - t.remaining;   // wins that must land on other teams
        int above = 0, room = 0;
        std::vector<int> extraIfPushed;  // extra wins a team soaks up once it's allowed over T
        for (auto& u : out) {
            if (&u == &t) continue;
            if (u.wins > T) { ++above; room += u.remaining; continue; }
            int stayBelow = std::min(u.remaining, T - u.wins);
            room += stayBelow;
            extraIfPushed.push_back(u.remaining - stayBelow);
        }
        std::sort(extraIfPushed.rbegin(), extraIfPushed.rend());
        for (size_t k = 0; room < toPlace && k < extraIfPushed.size(); ++k) {
            room += extraIfPushed[k];
            ++above;                     // this team is forced past T
        }
        return above >= playoffSpots;
    }

    // Orders out[i, j) — teams with identical series records — by a head-to-head
    // mini-table among only those teams. If that splits them into smaller tied
    // groups, head-to-head is re-applied within each smaller group. Only when
    // head-to-head can't separate teams at all do we fall back to overall
    // game differential, then games won, then name.
    void BreakTie(std::vector<Row>& out, size_t i, size_t j) const {
        std::map<std::string, int> h2hWins, h2hDiff;
        auto inGroup = [&](const std::string& n) {
            for (size_t k = i; k < j; ++k) if (out[k].name == n) return true;
            return false;
        };
        for (auto& m : matches) {
            if (!inGroup(m.a) || !inGroup(m.b)) continue;
            h2hWins[m.Winner()]++;
            int d = m.WinnerGames() - m.LoserGames();
            h2hDiff[m.Winner()] += d;
            h2hDiff[m.Loser()]  -= d;
        }
        auto key = [&](const Row& r) { return std::make_pair(h2hWins[r.name], h2hDiff[r.name]); };
        std::stable_sort(out.begin() + i, out.begin() + j,
                         [&](const Row& a, const Row& b) { return key(a) > key(b); });

        for (size_t s = i; s < j;) {
            size_t e = s + 1;
            while (e < j && key(out[e]) == key(out[s])) ++e;
            if (e - s > 1) {
                if (e - s < j - i) {
                    BreakTie(out, s, e);                    // smaller tie: re-apply head-to-head
                } else {
                    std::sort(out.begin() + s, out.begin() + e, [](const Row& a, const Row& b) {
                        if (a.GameDiff() != b.GameDiff()) return a.GameDiff() > b.GameDiff();
                        if (a.gamesWon != b.gamesWon) return a.gamesWon > b.gamesWon;
                        return a.name < b.name;
                    });
                }
            }
            if (e < j) out[e - 1].wonOnH2H = true;          // separated from the next team by H2H
            s = e;
        }
    }

    void Save() const {
        std::ofstream f(kSaveFile);
        f << "N\t" << title << "\n";
        f << "P\t" << playoffSpots << "\n";
        f << "G\t" << seriesPerTeam << "\n";   // older versions skip this line
        for (auto& t : teams) f << "T\t" << t << "\n";
        for (auto& m : matches)
            f << "S\t" << m.a << "\t" << m.b << "\t" << m.gamesA << "\t" << m.gamesB << "\n";
    }

    void Load() {
        std::ifstream f(kSaveFile);
        if (!f) return;
        teams.clear(); matches.clear();
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::vector<std::string> parts;
            std::stringstream ss(line);
            std::string p;
            while (std::getline(ss, p, '\t')) parts.push_back(p);
            if (parts.size() < 2) continue;
            if (parts[0] == "N") title = parts[1];
            else if (parts[0] == "P") playoffSpots = std::max(0, std::atoi(parts[1].c_str()));
            else if (parts[0] == "G") seriesPerTeam = std::max(1, std::atoi(parts[1].c_str()));
            else if (parts[0] == "T" && !HasTeam(parts[1])) teams.push_back(parts[1]);
            else if (parts[0] == "S" && parts.size() >= 5) {
                // Best-of-3 series (current format)
                int ga = std::atoi(parts[3].c_str()), gb = std::atoi(parts[4].c_str());
                if (ga != gb) matches.push_back({parts[1], parts[2], ga, gb});
            } else if (parts[0] == "M" && parts.size() >= 4) {
                // First version: one result per line, "M blue red winner(0|1)", no game score.
                // Loaded as a series win with a 1-0 game score; it's rewritten as an S line on next save.
                bool aWon = parts[3] != "1";
                matches.push_back({parts[1], parts[2], aWon ? 1 : 0, aWon ? 0 : 1});
            }
        }
    }
};

// ---------------------------------------------------------------- helpers

static std::string Clean(const char* s) {
    std::string r;
    for (const char* p = s; *p; ++p)
        if (*p != '\t' && *p != '\n' && *p != '\r') r += *p;
    size_t a = r.find_first_not_of(' '), b = r.find_last_not_of(' ');
    return a == std::string::npos ? "" : r.substr(a, b - a + 1);
}

static void TeamCombo(const char* label, const std::vector<std::string>& teams, int& idx) {
    if (idx >= (int)teams.size()) idx = -1;
    const char* preview = idx >= 0 ? teams[idx].c_str() : "Select team...";
    if (ImGui::BeginCombo(label, preview)) {
        for (int i = 0; i < (int)teams.size(); ++i)
            if (ImGui::Selectable(teams[i].c_str(), i == idx)) idx = i;
        ImGui::EndCombo();
    }
}

static void ApplyTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0; s.FrameRounding = 4; s.ChildRounding = 6; s.GrabRounding = 4;
    s.CellPadding = ImVec2(10, 6);
    ImVec4* c = s.Colors;
    ImVec4 gold(0.78f, 0.67f, 0.43f, 1.0f);            // LoL-ish gold
    c[ImGuiCol_WindowBg]        = ImVec4(0.04f, 0.07f, 0.11f, 1.0f);
    c[ImGuiCol_ChildBg]         = ImVec4(0.06f, 0.10f, 0.15f, 1.0f);
    c[ImGuiCol_Button]          = ImVec4(0.12f, 0.24f, 0.33f, 1.0f);
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.18f, 0.34f, 0.45f, 1.0f);
    c[ImGuiCol_ButtonActive]    = ImVec4(0.24f, 0.42f, 0.55f, 1.0f);
    c[ImGuiCol_Header]          = ImVec4(0.12f, 0.24f, 0.33f, 1.0f);
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.18f, 0.34f, 0.45f, 1.0f);
    c[ImGuiCol_TableHeaderBg]   = ImVec4(0.10f, 0.17f, 0.24f, 1.0f);
    c[ImGuiCol_Tab]             = ImVec4(0.10f, 0.17f, 0.24f, 1.0f);
    c[ImGuiCol_TabSelected]     = ImVec4(0.18f, 0.34f, 0.45f, 1.0f);
    c[ImGuiCol_TabHovered]      = ImVec4(0.24f, 0.42f, 0.55f, 1.0f);
    c[ImGuiCol_FrameBg]         = ImVec4(0.10f, 0.17f, 0.24f, 1.0f);
    c[ImGuiCol_SliderGrab]      = gold;
    c[ImGuiCol_CheckMark]       = gold;
    c[ImGuiCol_Separator]       = ImVec4(0.78f, 0.67f, 0.43f, 0.4f);
}

// ---------------------------------------------------------------- UI panels

static void DrawStandings(League& lg) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.67f, 0.43f, 1.0f));
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted(lg.title.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::TextDisabled("%d teams  |  %d series played  |  best of 3", (int)lg.teams.size(), (int)lg.matches.size());
    ImGui::Separator();
    ImGui::Spacing();

    auto rows = lg.Standings();
    if (rows.empty()) {
        ImGui::TextDisabled("No teams yet. Add some in the Teams tab ->");
        return;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    const ImVec4 kClinched(0.40f, 0.65f, 1.00f, 1.0f);
    const ImVec4 kEliminated(0.95f, 0.35f, 0.35f, 1.0f);
    float legendH = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

    if (ImGui::BeginTable("standings", 8, flags, ImVec2(0, -legendH))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#",      ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Team",   ImGuiTableColumnFlags_WidthStretch, 4.0f);
        ImGui::TableSetupColumn("W",      ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("L",      ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("Left",   ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("Games",  ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("+/-",    ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Streak", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)rows.size(); ++i) {
            const Row& r = rows[i];
            ImGui::TableNextRow(0, ImGui::GetTextLineHeight() * 1.6f);
            bool inPlayoffs = i < lg.playoffSpots;
            if (inPlayoffs)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(200, 170, 110, 40));

            ImGui::TableSetColumnIndex(0);
            if (inPlayoffs) ImGui::TextColored(ImVec4(0.78f, 0.67f, 0.43f, 1), "%d", i + 1);
            else            ImGui::Text("%d", i + 1);
            ImGui::TableSetColumnIndex(1);
            if (r.status > 0)      ImGui::TextColored(kClinched,   "%s", r.name.c_str());
            else if (r.status < 0) ImGui::TextColored(kEliminated, "%s", r.name.c_str());
            else                   ImGui::TextUnformatted(r.name.c_str());
            if (r.status != 0 && ImGui::IsItemHovered())
                ImGui::SetTooltip(r.status > 0 ? "Clinched a playoff spot" : "Eliminated from playoffs");
            if (r.wonOnH2H) {
                ImGui::SameLine();
                ImGui::TextDisabled("(H2H)");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Tied record with the team below; ranked higher on head-to-head");
            }
            ImGui::TableSetColumnIndex(2); ImGui::Text("%d", r.wins);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%d", r.losses);
            ImGui::TableSetColumnIndex(4);
            if (r.remaining > 0) ImGui::Text("%d", r.remaining);
            else                 ImGui::TextDisabled("done");
            ImGui::TableSetColumnIndex(5); ImGui::Text("%d-%d", r.gamesWon, r.gamesLost);
            ImGui::TableSetColumnIndex(6);
            if (r.GameDiff() > 0)      ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1), "+%d", r.GameDiff());
            else if (r.GameDiff() < 0) ImGui::TextColored(ImVec4(0.90f, 0.35f, 0.35f, 1), "%d", r.GameDiff());
            else                       ImGui::TextDisabled("0");
            ImGui::TableSetColumnIndex(7);
            if (r.streakLen == 0) ImGui::TextDisabled("-");
            else ImGui::TextColored(r.streakType == 'W' ? ImVec4(0.35f, 0.85f, 0.45f, 1)
                                                        : ImVec4(0.90f, 0.35f, 0.35f, 1),
                                    "%c%d", r.streakType, r.streakLen);
        }
        ImGui::EndTable();
    }
    ImGui::TextColored(kClinched, "Blue");
    ImGui::SameLine(); ImGui::TextDisabled("= clinched playoffs    ");
    ImGui::SameLine(); ImGui::TextColored(kEliminated, "Red");
    ImGui::SameLine(); ImGui::TextDisabled("= eliminated    (top %d advance, %d series each)",
                                           lg.playoffSpots, lg.seriesPerTeam);
}

static void DrawRecordTab(League& lg) {
    static int teamA = -1, teamB = -1;
    static std::string status;

    if (lg.teams.size() < 2) { ImGui::TextDisabled("Add at least two teams first."); return; }

    ImGui::TextUnformatted("Team 1");
    ImGui::SetNextItemWidth(-1); TeamCombo("##teamA", lg.teams, teamA);
    ImGui::Spacing();
    ImGui::TextUnformatted("Team 2");
    ImGui::SetNextItemWidth(-1); TeamCombo("##teamB", lg.teams, teamB);
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    bool valid = teamA >= 0 && teamB >= 0 && teamA != teamB;
    if (teamA >= 0 && teamA == teamB) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "A team can't play itself.");
    for (int idx : {teamA, teamB}) {
        if (idx < 0 || lg.Played(lg.teams[idx]) < lg.seriesPerTeam) continue;
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "%s has already played all %d series.",
                           lg.teams[idx].c_str(), lg.seriesPerTeam);
        valid = false;
    }

    // Series result buttons: winner + score (2-0 sweep or 2-1).
    int gA = -1, gB = -1;
    float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(!valid);
    for (int side = 0; side < 2; ++side) {
        int idx = side == 0 ? teamA : teamB;
        const char* name = (valid && idx >= 0) ? lg.teams[idx].c_str() : (side == 0 ? "Team 1" : "Team 2");
        ImGui::TextColored(ImVec4(0.78f, 0.67f, 0.43f, 1), "%s wins the series", name);
        ImGui::PushID(side);
        ImGui::PushStyleColor(ImGuiCol_Button, side == 0 ? ImVec4(0.15f, 0.30f, 0.60f, 1)
                                                         : ImVec4(0.60f, 0.18f, 0.18f, 1));
        if (ImGui::Button("2 - 0", ImVec2(w, 40))) { gA = side == 0 ? 2 : 0; gB = side == 0 ? 0 : 2; }
        ImGui::SameLine();
        if (ImGui::Button("2 - 1", ImVec2(w, 40))) { gA = side == 0 ? 2 : 1; gB = side == 0 ? 1 : 2; }
        ImGui::PopStyleColor();
        ImGui::PopID();
        ImGui::Spacing();
    }
    ImGui::EndDisabled();

    if (gA >= 0) {
        Match m{lg.teams[teamA], lg.teams[teamB], gA, gB};
        lg.matches.push_back(m);
        lg.Save();
        status = "Recorded: " + m.Winner() + " def. " + m.Loser() + "  " +
                 std::to_string(m.WinnerGames()) + "-" + std::to_string(m.LoserGames());
        teamA = teamB = -1;
    }
    if (!status.empty()) { ImGui::Spacing(); ImGui::TextWrapped("%s", status.c_str()); }
}

static void DrawTeamsTab(League& lg) {
    static char nameBuf[64] = "";
    static std::string error;

    ImGui::SetNextItemWidth(-80);
    bool enter = ImGui::InputTextWithHint("##newteam", "Team name", nameBuf, sizeof(nameBuf),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Add", ImVec2(-1, 0)) || enter) {
        std::string n = Clean(nameBuf);
        if (n.empty()) error = "Enter a name.";
        else if (lg.HasTeam(n)) error = "That team already exists.";
        else { lg.teams.push_back(n); lg.Save(); nameBuf[0] = 0; error.clear(); }
    }
    if (!error.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "%s", error.c_str());
    ImGui::Separator();

    int removeIdx = -1;
    for (int i = 0; i < (int)lg.teams.size(); ++i) {
        ImGui::PushID(i);
        bool locked = lg.TeamHasMatches(lg.teams[i]);
        ImGui::BeginDisabled(locked);
        if (ImGui::SmallButton("Remove")) removeIdx = i;
        ImGui::EndDisabled();
        if (locked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Delete this team's matches in History first.");
        ImGui::SameLine();
        ImGui::TextUnformatted(lg.teams[i].c_str());
        ImGui::PopID();
    }
    if (removeIdx >= 0) { lg.teams.erase(lg.teams.begin() + removeIdx); lg.Save(); }
}

static void DrawHistoryTab(League& lg) {
    if (lg.matches.empty()) { ImGui::TextDisabled("No matches recorded yet."); return; }
    int removeIdx = -1;
    for (int i = (int)lg.matches.size() - 1; i >= 0; --i) {   // newest first
        const Match& m = lg.matches[i];
        ImGui::PushID(i);
        if (ImGui::SmallButton("X")) removeIdx = i;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete this result");
        ImGui::SameLine();
        ImGui::TextDisabled("#%d", i + 1);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1), "%s", m.Winner().c_str());
        ImGui::SameLine(); ImGui::Text("%d-%d", m.WinnerGames(), m.LoserGames());
        ImGui::SameLine(); ImGui::TextUnformatted(m.Loser().c_str());
        ImGui::PopID();
    }
    if (removeIdx >= 0) { lg.matches.erase(lg.matches.begin() + removeIdx); lg.Save(); }
}

static void DrawSettingsTab(League& lg) {
    static char titleBuf[128];
    static bool init = false;
    if (!init) { snprintf(titleBuf, sizeof(titleBuf), "%s", lg.title.c_str()); init = true; }

    ImGui::TextUnformatted("Tournament name");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##title", titleBuf, sizeof(titleBuf))) { lg.title = Clean(titleBuf); lg.Save(); }

    ImGui::Spacing();
    ImGui::TextUnformatted("Playoff spots (highlighted)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputInt("##spots", &lg.playoffSpots)) {
        lg.playoffSpots = std::max(0, lg.playoffSpots); lg.Save();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Regular-season series per team");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputInt("##series", &lg.seriesPerTeam)) {
        lg.seriesPerTeam = std::max(1, lg.seriesPerTeam); lg.Save();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Text size (bump this up for a projector)");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##scale", &ImGui::GetIO().FontGlobalScale, 0.75f, 2.5f, "%.2fx");

    ImGui::Spacing(); ImGui::Separator();
    ImGui::TextDisabled("Data saves automatically to %s", kSaveFile);
}

// ---------------------------------------------------------------- main

int main() {
    if (!glfwInit()) return 1;

#ifdef __APPLE__
    const char* glsl = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glsl = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 760, "LoL Intramural Standings", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // don't litter imgui.ini
    ImFontConfig cfg; cfg.SizePixels = 20.0f;
    io.Fonts->AddFontDefault(&cfg);
    ApplyTheme();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl);

    League lg;
    lg.Load();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoBringToFrontOnFocus);

        float panelW = std::max(320.0f, ImGui::GetContentRegionAvail().x * 0.32f);
        ImGui::BeginChild("standings", ImVec2(-panelW - ImGui::GetStyle().ItemSpacing.x, 0),
                          ImGuiChildFlags_AlwaysUseWindowPadding);
        DrawStandings(lg);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("controls", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Record"))   { DrawRecordTab(lg);   ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Teams"))    { DrawTeamsTab(lg);    ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("History"))  { DrawHistoryTab(lg);  ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Settings")) { DrawSettingsTab(lg); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
        ImGui::End();

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.04f, 0.07f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    lg.Save();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
