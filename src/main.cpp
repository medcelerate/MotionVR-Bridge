#include "app.h"

#include "core/Bridge.h"
#include "core/Plugin.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#else
#include <cstdio>
#include <cstdlib>
#endif

namespace {

// Where each role sits on the body outline (fractions of the figure). The
// figure faces the viewer, so the subject's left side is drawn on the right.
struct RoleLayout {
    mvr::TrackerRole role;
    float x, y;
};

constexpr RoleLayout kLayout[] = {
    {mvr::TrackerRole::Head, 0.50f, 0.085f},
    {mvr::TrackerRole::Chest, 0.50f, 0.27f},
    {mvr::TrackerRole::Hip, 0.50f, 0.52f},
    {mvr::TrackerRole::RightElbow, 0.24f, 0.41f},
    {mvr::TrackerRole::LeftElbow, 0.76f, 0.41f},
    {mvr::TrackerRole::RightHand, 0.16f, 0.61f},
    {mvr::TrackerRole::LeftHand, 0.84f, 0.61f},
    {mvr::TrackerRole::RightKnee, 0.405f, 0.75f},
    {mvr::TrackerRole::LeftKnee, 0.595f, 0.75f},
    {mvr::TrackerRole::RightFoot, 0.41f, 0.94f},
    {mvr::TrackerRole::LeftFoot, 0.59f, 0.94f},
};

std::shared_ptr<slint::VectorModel<ui::ConfigField>> toUiConfig(const mvr::Config& cfg)
{
    std::vector<ui::ConfigField> rows;
    for (const mvr::ConfigField& f : cfg) {
        ui::ConfigField row;
        row.label = slint::SharedString(f.label);
        row.kind = static_cast<int>(f.kind);
        row.value = slint::SharedString(f.value);
        row.hint = slint::SharedString(f.hint);
        auto options = std::make_shared<slint::VectorModel<slint::SharedString>>();
        for (const std::string& o : f.options)
            options->push_back(slint::SharedString(o));
        row.options = options;
        row.selected = static_cast<int>(std::find(f.options.begin(), f.options.end(), f.value) - f.options.begin());
        rows.push_back(row);
    }
    return std::make_shared<slint::VectorModel<ui::ConfigField>>(std::move(rows));
}

std::shared_ptr<slint::VectorModel<slint::SharedString>> toUiNames(const auto& infos)
{
    auto model = std::make_shared<slint::VectorModel<slint::SharedString>>();
    for (const auto& info : infos)
        model->push_back(slint::SharedString(info.name));
    return model;
}

// Opens an http(s) link in the default browser.
void openUrl(const std::string& url)
{
    if ((url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) || url.find_first_of("'\"") != std::string::npos)
        return;
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
#ifdef __APPLE__
    const std::string command = "open '" + url + "'";
#else
    const std::string command = "xdg-open '" + url + "' &";
#endif
    if (std::system(command.c_str()) != 0)
        std::fprintf(stderr, "Could not open %s\n", url.c_str());
#endif
}

} // namespace

int main()
{
    const auto& sources = mvr::availableSources();
    const auto& sinks = mvr::availableSinks();

    // Per-plugin settings survive switching the dropdown back and forth.
    std::vector<mvr::Config> sourceConfigs, sinkConfigs;
    for (const auto& s : sources)
        sourceConfigs.push_back(s.create()->defaultConfig());
    // Kept to answer supportedRoles() for the current settings.
    std::vector<std::unique_ptr<mvr::TrackingSink>> sinkPrototypes;
    for (const auto& s : sinks) {
        sinkPrototypes.push_back(s.create());
        sinkConfigs.push_back(sinkPrototypes.back()->defaultConfig());
    }

    mvr::Bridge bridge;
    auto ui = ui::AppWindow::create();

    ui->set_source_names(toUiNames(sources));
    ui->set_sink_names(toUiNames(sinks));
    ui->set_source_config(toUiConfig(sourceConfigs[0]));
    ui->set_sink_config(toUiConfig(sinkConfigs[0]));

    auto trackers = std::make_shared<slint::VectorModel<ui::TrackerIndicator>>();
    for (const RoleLayout& l : kLayout) {
        ui::TrackerIndicator t;
        t.name = slint::SharedString(std::string(mvr::roleName(l.role)));
        t.x = l.x;
        t.y = l.y;
        trackers->push_back(t);
    }
    ui->set_trackers(trackers);

    auto refreshTrackers = [&, weak = slint::ComponentWeakHandle(ui)] {
        const auto snap = bridge.snapshot();
        const int sinkIndex = (*weak.lock())->get_sink_index();
        const mvr::RoleSet supported = sinkPrototypes[sinkIndex]->supportedRoles(sinkConfigs[sinkIndex]);
        for (size_t i = 0; i < std::size(kLayout); ++i) {
            const int r = static_cast<int>(kLayout[i].role);
            ui::TrackerIndicator t = *trackers->row_data(i);
            t.live = snap.roles[r].live;
            t.activity = snap.roles[r].activity;
            t.enabled = bridge.roleEnabled(kLayout[i].role);
            t.supported = supported.test(r);
            trackers->set_row_data(i, t);
        }
        return snap;
    };

    ui->on_source_selected([&, weak = slint::ComponentWeakHandle(ui)](int index) {
        (*weak.lock())->set_source_config(toUiConfig(sourceConfigs[index]));
    });
    ui->on_sink_selected([&, weak = slint::ComponentWeakHandle(ui)](int index) {
        (*weak.lock())->set_sink_config(toUiConfig(sinkConfigs[index]));
        refreshTrackers();
    });
    ui->on_source_config_edited([&, weak = slint::ComponentWeakHandle(ui)](int field, slint::SharedString value) {
        sourceConfigs[(*weak.lock())->get_source_index()][field].value = std::string(value);
    });
    ui->on_sink_config_edited([&, weak = slint::ComponentWeakHandle(ui)](int field, slint::SharedString value) {
        sinkConfigs[(*weak.lock())->get_sink_index()][field].value = std::string(value);
        refreshTrackers(); // e.g. toggling hands changes which points are used
    });
    ui->on_source_action([&, weak = slint::ComponentWeakHandle(ui)](int field) {
        bridge.runSourceAction(sourceConfigs[(*weak.lock())->get_source_index()][field].key);
    });
    ui->on_sink_action([&, weak = slint::ComponentWeakHandle(ui)](int field) {
        bridge.runSinkAction(sinkConfigs[(*weak.lock())->get_sink_index()][field].key);
    });
    ui->on_tracker_clicked([&](int index) {
        const mvr::TrackerRole role = kLayout[index].role;
        bridge.setRoleEnabled(role, !bridge.roleEnabled(role));
        refreshTrackers();
    });
    ui->on_start_stop_clicked([&, weak = slint::ComponentWeakHandle(ui)] {
        auto w = *weak.lock();
        w->set_error_text("");
        if (bridge.running()) {
            bridge.stop();
        } else {
            const int src = w->get_source_index();
            const int dst = w->get_sink_index();
            std::string error;
            if (!bridge.start(sources[src].create(), sourceConfigs[src], sinks[dst].create(), sinkConfigs[dst], error))
                w->set_error_text(slint::SharedString(error));
        }
        refreshTrackers();
    });

    ui->on_open_url([](slint::SharedString url) { openUrl(std::string(url)); });

    slint::Timer uiTimer(std::chrono::milliseconds(33), [&, weak = slint::ComponentWeakHandle(ui)] {
        auto w = *weak.lock();
        const auto snap = refreshTrackers();
        w->set_running(snap.running);
        w->set_frame_rate(snap.frameRate);
        w->set_source_status(slint::SharedString(snap.sourceStatus));
        w->set_sink_status(slint::SharedString(snap.sinkStatus));
    });

    ui->run();
    bridge.stop();
    return 0;
}
