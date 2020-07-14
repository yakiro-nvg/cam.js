#include <node_api.h>

namespace cam { namespace native {

void CamInit      (napi_env env, napi_value exports);
void AssemblerInit(napi_env env, napi_value exports);

static napi_value Init(napi_env env, napi_value exports)
{
        CamInit      (env, exports);
        AssemblerInit(env, exports);
        return exports;
}

NAPI_MODULE(cam_native, Init)

} } // namespace cam::native
