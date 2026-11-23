#include "render/glcore.hpp"

namespace hindgl {

#define HIND_GL_DEFINE(ret, name, params) ret(HIND_GLAPI* gl##name) params = nullptr;
HIND_GL_FUNCTIONS(HIND_GL_DEFINE)
#undef HIND_GL_DEFINE

bool load(void* (*getProcAddress)(const char*), std::string& missing) {
#define HIND_GL_LOAD(ret, name, params)                                                   \
  gl##name = reinterpret_cast<ret(HIND_GLAPI*) params>(getProcAddress("gl" #name));     \
  if (!gl##name) {                                                                        \
    missing = "gl" #name;                                                                 \
    return false;                                                                         \
  }
  HIND_GL_FUNCTIONS(HIND_GL_LOAD)
#undef HIND_GL_LOAD
  return true;
}

}  // namespace hindgl
