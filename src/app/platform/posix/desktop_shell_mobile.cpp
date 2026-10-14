// Phones: the OS owns the window, the screen and the process's identity, so
// there is nothing for the desktop shell to do. Named by the Android and iOS
// fragments. The screens' request to keep the display awake is answered "not
// held" until the mobile shell implements it (E4b).
#include "interfaces/desktop_shell.hpp"

namespace cube::platform::desktop_shell {

namespace {

class NoKeepAwake final : public KeepAwakeBackend {
public:
    void acquire() override { report(false); }
    void release() override {}
};

}  // namespace

std::unique_ptr<KeepAwakeBackend> make_keep_awake_backend() {
    return std::make_unique<NoKeepAwake>();
}

std::string apply_process_identity(std::string_view /*app_user_model_id*/) {
    return {};
}

Traits traits() noexcept {
    return Traits{};
}

}  // namespace cube::platform::desktop_shell
