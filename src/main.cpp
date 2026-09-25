#include "app.h"

#include "core/Bridge.h"
#include "core/Plugin.h"
#include "record/RecordingManager.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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

    // Declared before the bridge: the bridge's source thread writes into it.
    mvr::record::RecordingManager recorder;
    recorder.applySettings(recorder.settings());
    mvr::Bridge bridge;
    bridge.setFrameTap([&](const mvr::TrackingFrame& f) { recorder.onFrame(f); });
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

    // Refreshes dropdown options that can change (e.g. the list of recorded
    // takes) while keeping the user's choices.
    auto refreshSourceOptions = [&, weak = slint::ComponentWeakHandle(ui)](bool updateUi) {
        const int index = (*weak.lock())->get_source_index();
        const mvr::Config fresh = sources[index].create()->defaultConfig();
        mvr::Config& cfg = sourceConfigs[index];
        for (size_t i = 0; i < cfg.size() && i < fresh.size(); ++i) {
            if (cfg[i].kind != mvr::ConfigField::Kind::Choice)
                continue;
            cfg[i].options = fresh[i].options;
            cfg[i].hint = fresh[i].hint;
            if (std::find(cfg[i].options.begin(), cfg[i].options.end(), cfg[i].value) == cfg[i].options.end())
                cfg[i].value = fresh[i].value;
        }
        if (updateUi)
            (*weak.lock())->set_source_config(toUiConfig(cfg));
    };

    ui->on_source_selected([&, weak = slint::ComponentWeakHandle(ui)](int index) {
        refreshSourceOptions(false);
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
            recorder.setSourceName(sources[src].name); // for takes started from OSC or sync too
            std::string error;
            if (!bridge.start(sources[src].create(), sourceConfigs[src], sinks[dst].create(), sinkConfigs[dst], error))
                w->set_error_text(slint::SharedString(error));
        }
        refreshTrackers();
    });

    ui->on_open_url([](slint::SharedString url) { openUrl(std::string(url)); });

    // Recording
    {
        const mvr::record::RecordingSettings s = recorder.settings();
        ui->set_rec_folder(slint::SharedString(s.folder));
        ui->set_rec_csv(s.csv);
        ui->set_rec_osc(s.oscControl);
        ui->set_rec_osc_port(slint::SharedString(std::to_string(s.oscPort)));
        ui->set_rec_sync(s.sync);
        ui->set_rec_name(slint::SharedString(s.instanceName));
    }
    ui->on_record_clicked([&] { recorder.toggle(); });
    ui->on_rec_setting([&, weak = slint::ComponentWeakHandle(ui)](slint::SharedString key, slint::SharedString value) {
        mvr::record::RecordingSettings s = recorder.settings();
        const std::string k(key), v(value);
        if (k == "folder") {
            s.folder = v;
        } else if (k == "csv") {
            s.csv = v == "true";
        } else if (k == "osc") {
            s.oscControl = v == "true";
        } else if (k == "osc_port") {
            char* end = nullptr;
            const long port = std::strtol(v.c_str(), &end, 10);
            if (v.empty() || *end != '\0' || port <= 0 || port > 65535)
                return; // keep the last valid port while typing
            s.oscPort = static_cast<uint16_t>(port);
        } else if (k == "sync") {
            s.sync = v == "true";
        } else if (k == "name") {
            s.instanceName = v;
        }
        recorder.applySettings(s);
        if (k == "folder")
            refreshSourceOptions(true);
    });

    slint::Timer uiTimer(std::chrono::milliseconds(33), [&, weak = slint::ComponentWeakHandle(ui)] {
        auto w = *weak.lock();
        const auto snap = refreshTrackers();
        w->set_running(snap.running);
        w->set_frame_rate(snap.frameRate);
        w->set_source_status(slint::SharedString(snap.sourceStatus));
        w->set_sink_status(slint::SharedString(snap.sinkStatus));

        const auto rec = recorder.status();
        if (w->get_recording() && !rec.recording && !snap.running)
            refreshSourceOptions(true); // a new take is available to play back
        w->set_recording(rec.recording);
        char time[16];
        const int secs = static_cast<int>(rec.seconds);
        std::snprintf(time, sizeof(time), "%d:%02d", secs / 60, secs % 60);
        w->set_record_time(time);
        w->set_record_message(slint::SharedString(rec.message));
        w->set_rec_network(slint::SharedString(rec.network));
        std::string peers;
        for (const auto& p : rec.peers)
            peers += (peers.empty() ? "Found: " : ", ") + p.name + " (" + p.address + ")";
        w->set_rec_peers(slint::SharedString(peers));
    });

    ui->run();
    bridge.stop();
    return 0;
}
