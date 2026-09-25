#pragma once

#include <memory>
#include <string>

namespace pfd {
class select_folder;
}

namespace mvr {

// Native "choose folder" dialog that doesn't block the UI: open() returns
// immediately and poll() reports the result once the dialog closes.
class FolderPicker {
public:
    FolderPicker();
    ~FolderPicker();

    bool busy() const { return dialog_ != nullptr; }
    void open(const std::string& title, const std::string& startFolder);

    // Returns true once when the dialog has closed; `folder` is empty if it
    // was cancelled.
    bool poll(std::string& folder);

private:
    std::unique_ptr<pfd::select_folder> dialog_;
};

} // namespace mvr
