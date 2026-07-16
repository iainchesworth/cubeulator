#include "interfaces/inference_env.hpp"

namespace cube::platform::inference_env {

EnvironmentInfo current() {
    // Resolving Context.getFilesDir()/getCacheDir() requires a JNI bridge;
    // lands alongside the native Android shell work.
    return EnvironmentInfo{.model_search_path = {}, .execution_providers = {"CPU", "NNAPI"}};
}

}  // namespace cube::platform::inference_env
