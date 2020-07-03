#include <cam.h>
#include <cam/memory.h>

#include <node_api.h>

#include <assert.h>
#include <vector>
#include <memory>
#include <map>

using namespace std;

#define DECLARE_NAPI_METHOD(name, func) { name, 0, func, 0, 0, 0, napi_default, 0 }

#define MAX_NAME_LENGTH 256

namespace cam { namespace native {

struct MainAction
{
        MainAction(napi_env env, napi_ref ref, napi_value recv)
                : env(env)
                , ref(ref)
                , recv(recv)
        {
                // nop
        }

        napi_env env;
        napi_ref ref;
        napi_value recv;
};

static void call_main_action(struct cam_s *, cam_aid_t aid, void *ud)
{
        napi_status status;
        auto ma = (MainAction*)ud;

        napi_value action;
        status = napi_get_reference_value(ma->env, ma->ref, &action);
        assert(status == napi_ok);

        napi_value arg_aid;
        status = napi_create_uint32(ma->env, aid, &arg_aid);
        assert(status == napi_ok);

        status = napi_call_function(ma->env, ma->recv, action, 1, &arg_aid, nullptr);
        assert(status == napi_ok);
}

static void cleanup_main_actions(vector<shared_ptr<MainAction>> &actions)
{
        for (int i = 0; i < actions.size(); ++i) {
                auto &a = actions[i];
                napi_delete_reference(a->env, a->ref);
        }

        actions.clear();
}

struct chunk_allocator
{
        // `aif` must be at the head
        struct cam_alloc_if_s aif;
        map<const void*, napi_ref> buffers;
        napi_env env;
};

static void chunk_allocator_aif_dealloc(struct cam_alloc_s *a, void *p)
{
        auto ca = (chunk_allocator*)a;
        auto itr = ca->buffers.find(p);
        assert(itr != ca->buffers.end());
        napi_delete_reference(ca->env, itr->second);
        ca->buffers.erase(itr);
}

static void chunk_allocator_init(chunk_allocator &a, napi_env env)
{
        a.aif.malloc  = nullptr;
        a.aif.dealloc = &chunk_allocator_aif_dealloc;
        a.env = env;
}

static void chunk_allocator_drop(chunk_allocator &a)
{
        assert(a.buffers.empty());
}

static const void* chunk_allocator_take(chunk_allocator &a, napi_value buf)
{
        napi_status status;

        void *chunk;
        size_t chunk_sz;
        status = napi_get_buffer_info(a.env, buf, &chunk, &chunk_sz);
        assert(status == napi_ok);

        napi_ref ref;
        status = napi_create_reference(a.env, buf, 1, &ref);
        assert(status == napi_ok);
        a.buffers[chunk] = ref;

        return chunk;
}

struct ForeignProgram
{
        napi_env env;
        napi_ref ref;
        napi_value recv;
        char module [MAX_NAME_LENGTH];
        char program[MAX_NAME_LENGTH];
        cam_foreign_program_t cfp;
};

static void call_foreign_program(struct cam_s *, cam_aid_t aid, void *ud)
{
        napi_status status;
        auto fp = (ForeignProgram*)ud;

        napi_value f;
        status = napi_get_reference_value(fp->env, fp->ref, &f);
        assert(status == napi_ok);

        napi_value arg_aid;
        status = napi_create_uint32(fp->env, aid, &arg_aid);
        assert(status == napi_ok);

        status = napi_call_function(fp->env, fp->recv, f, 1, &arg_aid, nullptr);
        assert(status == napi_ok);
}

static void release_foreign_programs(vector<shared_ptr<ForeignProgram>> &fps)
{
        for (int i = 0; i < fps.size(); ++i) {
                auto &fp = fps[i];
                napi_delete_reference(fp->env, fp->ref);
        }

        fps.clear();
}

class Cam
{
private:
        Cam(napi_env env)
                : _env(env)
                , _wrapper(nullptr)
                , _cam(nullptr)
        {
                cam_error_t ec;
                _cam = cam_init(&ec);
                assert(ec == CEC_SUCCESS);
                chunk_allocator_init(_chunk_allocator, env);
        }

       ~Cam()
        {
                cam_drop(_cam);
                release_foreign_programs(_foreign_programs);
                chunk_allocator_drop(_chunk_allocator);
                napi_delete_reference(_env, _wrapper);
        }

        static napi_value New(napi_env env, napi_callback_info info)
        {
                napi_status status;

                napi_value jsthis;
                status = napi_get_cb_info(env, info, nullptr, nullptr, &jsthis, nullptr);
                assert(status == napi_ok);

                Cam *obj = new Cam(env);
                status = napi_wrap(env, jsthis, (void*)obj, &Cam::Destructor, nullptr, &obj->_wrapper);
                assert(status == napi_ok);

                return jsthis;
        }

        static napi_value Dispatch(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 1;
                napi_value jsthis, action;
                status = napi_get_cb_info(env, info, &argc, &action, &jsthis, nullptr);
                assert(status == napi_ok && argc == 1);

                napi_ref action_ref;
                status = napi_create_reference(env, action, 1, &action_ref);
                assert(status == napi_ok);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                obj->_main_actions.push_back(make_shared<MainAction>(obj->_env, action_ref, jsthis));
                cam_dispatch(obj->_cam, &call_main_action, obj->_main_actions.back().get());

                return nullptr;
        }

        static napi_value AddChunkBuffer(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 1;
                napi_value jsthis, chunk_buffer;
                status = napi_get_cb_info(env, info, &argc, &chunk_buffer, &jsthis, nullptr);
                assert(status == napi_ok && argc == 1);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                napi_value ret;
                const void *chunk = chunk_allocator_take(obj->_chunk_allocator, chunk_buffer);
                cam_error_t ec = cam_add_chunk(obj->_cam, chunk, (struct cam_alloc_s*)&obj->_chunk_allocator);
                status = napi_create_int32(env, ec, &ret);
                return ret;
        }

        static napi_value AddForeign(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 3;
                napi_value jsthis, argv[3];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 3);

                auto fp = make_shared<ForeignProgram>();

                fp->env = env;

                status = napi_create_reference(env, argv[2], 1, &fp->ref);
                assert(status == napi_ok);

                fp->recv = jsthis;

                size_t name_sz;

                status = napi_get_value_string_latin1(env, argv[0], nullptr, 0, &name_sz);
                assert(status == napi_ok && name_sz < MAX_NAME_LENGTH);
                status = napi_get_value_string_latin1(env, argv[0], fp->module,  MAX_NAME_LENGTH, &name_sz);
                assert(status == napi_ok);

                status = napi_get_value_string_latin1(env, argv[1], nullptr, 0, &name_sz);
                assert(status == napi_ok && name_sz < MAX_NAME_LENGTH);
                status = napi_get_value_string_latin1(env, argv[1], fp->program, MAX_NAME_LENGTH, &name_sz);
                assert(status == napi_ok);

                fp->cfp.module  = fp->module;
                fp->cfp.program = fp->program;
                fp->cfp.func    = &call_foreign_program;
                fp->cfp.ud      = fp.get();

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                obj->_foreign_programs.push_back(fp);
                cam_add_foreign(obj->_cam, &fp->cfp);

                return nullptr;
        }

        static napi_value Link(napi_env env, napi_callback_info info)
        {
                napi_status status;

                napi_value jsthis;
                status = napi_get_cb_info(env, info, nullptr, nullptr, &jsthis, nullptr);
                assert(status == napi_ok);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                napi_value ret;
                cam_error_t ec = cam_link(obj->_cam);
                status = napi_create_int32(env, ec, &ret);
                return ret;
        }

        static napi_value EnsureSlots(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 2;
                napi_value jsthis, argv[2];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 2);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                cam_aid_t aid;
                status = napi_get_value_uint32(env, argv[0], &aid);
                assert(status == napi_ok);

                uint32_t num_slots;
                status = napi_get_value_uint32(env, argv[1], &num_slots);
                cam_ensure_slots(obj->_cam, aid, num_slots);

                return nullptr;
        }

        static napi_value NumSlots(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 1;
                napi_value jsthis, argv[1];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 1);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                cam_aid_t aid;
                status = napi_get_value_uint32(env, argv[0], &aid);
                assert(status == napi_ok);

                napi_value ret;
                status = napi_create_int32(env, cam_num_slots(obj->_cam, aid), &ret);
                return ret;
        }

        static napi_value SetSlotProgram(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 4;
                napi_value jsthis, argv[4];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 4);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                cam_aid_t aid;
                status = napi_get_value_uint32(env, argv[0], &aid);
                assert(status == napi_ok);

                int32_t slot;
                status = napi_get_value_int32(env, argv[1], &slot);
                assert(status == napi_ok);

                size_t name_sz;
                char module [MAX_NAME_LENGTH];
                char program[MAX_NAME_LENGTH];

                status = napi_get_value_string_latin1(env, argv[2], nullptr, 0, &name_sz);
                assert(status == napi_ok && name_sz < MAX_NAME_LENGTH);
                status = napi_get_value_string_latin1(env, argv[2], module,  MAX_NAME_LENGTH, &name_sz);
                assert(status == napi_ok);

                status = napi_get_value_string_latin1(env, argv[3], nullptr, 0, &name_sz);
                assert(status == napi_ok && name_sz < MAX_NAME_LENGTH);
                status = napi_get_value_string_latin1(env, argv[3], program, MAX_NAME_LENGTH, &name_sz);
                assert(status == napi_ok);

                napi_value ret;
                cam_error_t ec = cam_set_slot_program(obj->_cam, aid, slot, module, program);
                status = napi_create_int32(env, ec, &ret);
                return ret;
        }

        static napi_value SetSlotComp4(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 3;
                napi_value jsthis, argv[3];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 3);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                cam_aid_t aid;
                status = napi_get_value_uint32(env, argv[0], &aid);
                assert(status == napi_ok);

                int32_t slot;
                status = napi_get_value_int32(env, argv[1], &slot);
                assert(status == napi_ok);

                napi_value c4v;

                bool lossless;
                cam_comp_4_t value;
                status = napi_get_named_property(env, argv[2], "value", &c4v);
                assert(status == napi_ok);
                status = napi_get_value_bigint_int64(env, c4v, &value, &lossless);
                assert(status == napi_ok && lossless);

                int precision;
                status = napi_get_named_property(env, argv[2], "precision", &c4v);
                assert(status == napi_ok);
                status = napi_get_value_int32(env, c4v, &precision);
                assert(status == napi_ok);

                int scale;
                status = napi_get_named_property(env, argv[2], "scale", &c4v);
                assert(status == napi_ok);
                status = napi_get_value_int32(env, c4v, &scale);
                assert(status == napi_ok);

                cam_set_slot_comp_4(obj->_cam, aid, slot, value, precision, scale);

                return nullptr;
        }

        static napi_value GetSlotComp4(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 2;
                napi_value jsthis, argv[2];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 2);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                cam_aid_t aid;
                status = napi_get_value_uint32(env, argv[0], &aid);
                assert(status == napi_ok);

                int32_t slot;
                status = napi_get_value_int32(env, argv[1], &slot);
                assert(status == napi_ok);

                int precision, scale;
                cam_comp_4_t value = cam_get_slot_comp_4(obj->_cam, aid, slot, &precision, &scale);

                napi_value ret;
                status = napi_create_object(env, &ret);
                assert(status == napi_ok);

                napi_value c4v;

                status = napi_create_bigint_int64(env, value, &c4v);
                assert(status == napi_ok);
                status = napi_set_named_property(env, ret, "value", c4v);

                status = napi_create_int32(env, precision, &c4v);
                assert(status == napi_ok);
                status = napi_set_named_property(env, ret, "precision", c4v);

                status = napi_create_int32(env, scale, &c4v);
                assert(status == napi_ok);
                status = napi_set_named_property(env, ret, "scale", c4v);

                return ret;
        }

        static napi_value Call(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 3;
                napi_value jsthis, argv[3];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 3);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                cam_aid_t aid;
                status = napi_get_value_uint32(env, argv[0], &aid);
                assert(status == napi_ok);

                int32_t num_usings;
                status = napi_get_value_int32(env, argv[1], &num_usings);
                assert(status == napi_ok);

                int32_t num_returnings;
                status = napi_get_value_int32(env, argv[1], &num_returnings);
                assert(status == napi_ok);

                cam_call(obj->_cam, aid, num_usings, num_returnings);

                return nullptr;
        }

        static napi_value RunOnce(napi_env env, napi_callback_info info)
        {
                napi_status status;

                napi_value jsthis;
                status = napi_get_cb_info(env, info, nullptr, nullptr, &jsthis, nullptr);
                assert(status == napi_ok);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                cam_run_once(obj->_cam);
                cleanup_main_actions(obj->_main_actions);

                return nullptr;
        }

        napi_env _env;
        napi_ref _wrapper;
        struct cam_s *_cam;
        vector<shared_ptr<MainAction>> _main_actions;
        vector<shared_ptr<ForeignProgram>> _foreign_programs;
        chunk_allocator _chunk_allocator;

public:
        static napi_value Init(napi_env env, napi_value exports)
        {
                napi_status status;

                const napi_property_descriptor props[] = {
                        DECLARE_NAPI_METHOD("dispatch",       &Dispatch),
                        DECLARE_NAPI_METHOD("addChunkBuffer", &AddChunkBuffer),
                        DECLARE_NAPI_METHOD("addForeign",     &AddForeign),
                        DECLARE_NAPI_METHOD("link",           &Link),
                        DECLARE_NAPI_METHOD("ensureSlots",    &EnsureSlots),
                        DECLARE_NAPI_METHOD("numSlots",       &NumSlots),
                        DECLARE_NAPI_METHOD("setSlotComp4",   &SetSlotComp4),
                        DECLARE_NAPI_METHOD("setSlotProgram", &SetSlotProgram),
                        DECLARE_NAPI_METHOD("getSlotComp4",   &GetSlotComp4),
                        DECLARE_NAPI_METHOD("call",           &Call),
                        DECLARE_NAPI_METHOD("runOnce",        &RunOnce)
                };

                const size_t num_props = sizeof(props) / sizeof(props[0]);

                napi_value cons;
                status = napi_define_class(
                        env, "CamNative", NAPI_AUTO_LENGTH, &New, nullptr, num_props, props, &cons);
                assert(status == napi_ok);

                status = napi_set_named_property(env, exports, "Cam", cons);
                assert(status == napi_ok);

                return exports;
        }

        static void Destructor(napi_env env, void *obj, void *)
        {
                ((Cam*)obj)->~Cam();
        }
};

napi_value Init(napi_env env, napi_value exports)
{
        return Cam::Init(env, exports);
}

NAPI_MODULE(cam_native, Init)

} } // namespace cam::native
