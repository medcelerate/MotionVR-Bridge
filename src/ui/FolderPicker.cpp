#include "ui/FolderPicker.h"

#ifdef _WIN32
#define NOMINMAX
#endif
#include "portable-file-dialogs.h"

namespace mvr {

FolderPicker::FolderPicker() = default;

FolderPicker::~FolderPicker()
{
    if (dialog_)
        dialog_->kill();
}

void FolderPicker::open(const std::string& title, const std::string& startFolder)
{
    if (!dialog_)
        dialog_ = std::make_unique<pfd::select_folder>(title, startFolder, pfd::opt::force_path);
}

bool FolderPicker::poll(std::string& folder)
{
    if (!dialog_ || !dialog_->ready(0))
        return false;
    folder = dialog_->result();
    dialog_.reset();
    return true;
}

} // namespace mvr
