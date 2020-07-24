#include <cam.h>
#include <cam/memory.h>

#include <node_api.h>

#include <assert.h>
#include <vector>
#include <memory>
#include <map>

using namespace std;

#define DECLARE_NAPI_METHOD(name, func) { name, 0, func, 0, 0, 0, napi_default, 0 }

namespace cam { namespace native {

static bool is_undefined(napi_env env, napi_value v)
{
        napi_valuetype t;
        napi_status status = napi_typeof(env, v, &t);
        assert(status == napi_ok);
        return t == napi_undefined;
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
        shared_ptr<char> module;
        shared_ptr<char> program;
        cam_foreign_program_t cfp;
};

static void call_foreign_program(struct cam_s *, int num_usings, void *ud)
{
        napi_status status;
        auto fp = (ForeignProgram*)ud;

        napi_value f;
        status = napi_get_reference_value(fp->env, fp->ref, &f);
        assert(status == napi_ok);

        napi_value argv[1];
        status = napi_create_int32(fp->env, num_usings, argv);
        assert(status == napi_ok);

        status = napi_call_function(fp->env, fp->recv, f, 1, argv, nullptr);
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

                size_t str_len, copied_len;

                status = napi_get_value_string_utf8(env, argv[0], nullptr, 0, &str_len);
                assert(status == napi_ok);
                fp->module.reset(new char[str_len + 1]);
                status = napi_get_value_string_utf8(env, argv[0], fp->module.get(), str_len + 1, &copied_len);
                assert(status == napi_ok && str_len == copied_len);

                status = napi_get_value_string_utf8(env, argv[1], nullptr, 0, &str_len);
                assert(status == napi_ok);
                fp->program.reset(new char[str_len + 1]);
                status = napi_get_value_string_utf8(env, argv[1], fp->program.get(), str_len + 1, &copied_len);
                assert(status == napi_ok && str_len == copied_len);

                fp->cfp.module  = fp->module.get();
                fp->cfp.program = fp->program.get();
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

                size_t argc = 1;
                napi_value jsthis, argv[1];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 1);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                int32_t num_slots;
                status = napi_get_value_int32(env, argv[0], &num_slots);
                cam_ensure_slots(obj->_cam, num_slots);

                return nullptr;
        }

        static napi_value NumSlots(napi_env env, napi_callback_info info)
        {
                napi_status status;

                napi_value jsthis;
                status = napi_get_cb_info(env, info, nullptr, nullptr, &jsthis, nullptr);
                assert(status == napi_ok);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                napi_value ret;
                status = napi_create_int32(env, cam_num_slots(obj->_cam), &ret);
                return ret;
        }

        static napi_value SlotType(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 1;
                napi_value jsthis, argv[1];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 1);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                int32_t slot;
                status = napi_get_value_int32(env, argv[0], &slot);
                assert(status == napi_ok);

                napi_value ret;
                status = napi_create_int32(env, cam_slot_type(obj->_cam, slot), &ret);
                return ret;
        }

        static napi_value SetSlotComp2(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 2;
                napi_value jsthis, argv[2];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 2);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                int32_t slot;
                status = napi_get_value_int32(env, argv[0], &slot);
                assert(status == napi_ok);

                double value;
                status = napi_get_value_double(env, argv[1], &value);
                cam_set_slot_comp_2(obj->_cam, slot, value);

                return nullptr;
        }

        static napi_value SetSlotComp4(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 2;
                napi_value jsthis, argv[2];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 2);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                int32_t slot;
                status = napi_get_value_int32(env, argv[0], &slot);
                assert(status == napi_ok);

                napi_value c4v;

                bool is_signed;
                status = napi_get_named_property(env, argv[1], "isSigned", &c4v);
                assert(status == napi_ok);
                status = napi_get_value_bool(env, c4v, &is_signed);
                assert(status == napi_ok);

                int scale;
                status = napi_get_named_property(env, argv[1], "scale", &c4v);
                assert(status == napi_ok);
                status = napi_get_value_int32(env, c4v, &scale);
                assert(status == napi_ok);

                bool lossless;
                cam_comp_4_t value;
                status = napi_get_named_property(env, argv[1], "value", &c4v);
                assert(status == napi_ok);
                status = napi_get_value_bigint_int64(env, c4v, &value, &lossless);
                assert(status == napi_ok && lossless);

                cam_set_slot_comp_4(obj->_cam, slot, is_signed, scale, value);

                return nullptr;
        }

        static napi_value SetSlotProgram(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 3;
                napi_value jsthis, argv[3];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 3);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                int32_t slot;
                status = napi_get_value_int32(env, argv[0], &slot);
                assert(status == napi_ok);

                size_t str_len, copied_len;

                status = napi_get_value_string_utf8(env, argv[1], nullptr, 0, &str_len);
                assert(status == napi_ok);
                shared_ptr<char> module(new char[str_len + 1]);
                status = napi_get_value_string_utf8(env, argv[1], module.get(), str_len + 1, &copied_len);
                assert(status == napi_ok && str_len == copied_len);

                status = napi_get_value_string_utf8(env, argv[2], nullptr, 0, &str_len);
                assert(status == napi_ok);
                shared_ptr<char> program(new char[str_len + 1]);
                status = napi_get_value_string_utf8(env, argv[2], program.get(), str_len + 1, &copied_len);
                assert(status == napi_ok && str_len == copied_len);

                napi_value ret;
                cam_error_t ec = cam_set_slot_program(obj->_cam, slot, module.get(), program.get());
                status = napi_create_int32(env, ec, &ret);
                return ret;
        }

        static napi_value SetSlotDisplay(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 2;
                napi_value jsthis, argv[2];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 1);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                int32_t slot;
                status = napi_get_value_int32(env, argv[0], &slot);
                assert(status == napi_ok);

                if (argc == 2 && !is_undefined(env, argv[1])) {
                        size_t display_len, copied_len;
                        status = napi_get_value_string_utf8(env, argv[1], nullptr, 0, &display_len);
                        assert(status == napi_ok);
                        char *str = cam_set_slot_display(obj->_cam, slot, nullptr, display_len);
                        status = napi_get_value_string_utf8(env, argv[1], str, display_len + 1, &copied_len);
                        assert(status == napi_ok && display_len == copied_len);
                } else {
                        cam_set_slot_display(obj->_cam, slot, "", 1);
                }

                return nullptr;
        }

        static napi_value GetSlotComp2(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 1;
                napi_value jsthis, argv[1];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 1);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                int32_t slot;
                status = napi_get_value_int32(env, argv[0], &slot);
                assert(status == napi_ok);

                napi_value ret;
                double value = cam_get_slot_comp_2(obj->_cam, slot);
                status = napi_create_double(env, value, &ret);
                assert(status == napi_ok);
                return ret;
        }

        static napi_value GetSlotComp4(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 1;
                napi_value jsthis, argv[1];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 1);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                int32_t slot;
                status = napi_get_value_int32(env, argv[0], &slot);
                assert(status == napi_ok);

                bool is_signed;
                int scale;
                cam_comp_4_t value = cam_get_slot_comp_4(obj->_cam, slot, &is_signed, &scale);

                napi_value c4v;
                status = napi_create_object(env, &c4v);
                assert(status == napi_ok);

                napi_value v;

                status = napi_get_boolean(env, is_signed, &v);
                assert(status == napi_ok);
                status = napi_set_named_property(env, c4v, "isSigned", v);

                status = napi_create_int32(env, scale, &v);
                assert(status == napi_ok);
                status = napi_set_named_property(env, c4v, "scale", v);

                status = napi_create_bigint_int64(env, value, &v);
                assert(status == napi_ok);
                status = napi_set_named_property(env, c4v, "value", v);

                return c4v;
        }

        static napi_value GetSlotDisplay(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 1;
                napi_value jsthis, argv[1];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 1);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                int32_t slot;
                status = napi_get_value_int32(env, argv[0], &slot);
                assert(status == napi_ok);

                int length;
                napi_value ret;
                const char *str = cam_get_slot_display(obj->_cam, slot, &length);
                status = napi_create_string_utf8(env, str, length, &ret);
                return ret;
        }

        static napi_value Call(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 2;
                napi_value jsthis, argv[2];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 2);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                int32_t num_usings;
                status = napi_get_value_int32(env, argv[0], &num_usings);
                assert(status == napi_ok);

                int32_t num_returnings;
                status = napi_get_value_int32(env, argv[1], &num_returnings);
                assert(status == napi_ok);

                cam_call(obj->_cam, num_usings, num_returnings);

                return nullptr;
        }

        static napi_value ProtectedCall(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 2;
                napi_value jsthis, argv[2];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 2);

                Cam *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                int32_t num_usings;
                status = napi_get_value_int32(env, argv[0], &num_usings);
                assert(status == napi_ok);

                int32_t num_returnings;
                status = napi_get_value_int32(env, argv[1], &num_returnings);
                assert(status == napi_ok);

                cam_protected_call(obj->_cam, num_usings, num_returnings);

                return nullptr;
        }

        napi_env _env;
        napi_ref _wrapper;
        struct cam_s *_cam;
        vector<shared_ptr<ForeignProgram>> _foreign_programs;
        chunk_allocator _chunk_allocator;

public:
        static void Init(napi_env env, napi_value exports)
        {
                napi_status status;

                const napi_property_descriptor props[] = {
                        DECLARE_NAPI_METHOD("addChunkBuffer", &AddChunkBuffer),
                        DECLARE_NAPI_METHOD("addForeign",     &AddForeign),
                        DECLARE_NAPI_METHOD("link",           &Link),
                        DECLARE_NAPI_METHOD("ensureSlots",    &EnsureSlots),
                        DECLARE_NAPI_METHOD("numSlots",       &NumSlots),
                        DECLARE_NAPI_METHOD("slotType",       &SlotType),
                        DECLARE_NAPI_METHOD("setSlotComp2",   &SetSlotComp2),
                        DECLARE_NAPI_METHOD("setSlotComp4",   &SetSlotComp4),
                        DECLARE_NAPI_METHOD("setSlotProgram", &SetSlotProgram),
                        DECLARE_NAPI_METHOD("setSlotDisplay", &SetSlotDisplay),
                        DECLARE_NAPI_METHOD("getSlotComp2",   &GetSlotComp2),
                        DECLARE_NAPI_METHOD("getSlotComp4",   &GetSlotComp4),
                        DECLARE_NAPI_METHOD("getSlotDisplay", &GetSlotDisplay),
                        DECLARE_NAPI_METHOD("call",           &Call),
                        DECLARE_NAPI_METHOD("protectedCall",  &ProtectedCall)
                };

                const size_t num_props = sizeof(props) / sizeof(props[0]);

                napi_value cons;
                status = napi_define_class(
                        env, "CamNative", NAPI_AUTO_LENGTH, &New, nullptr, num_props, props, &cons);
                assert(status == napi_ok);

                status = napi_set_named_property(env, exports, "CamNative", cons);
                assert(status == napi_ok);
        }

        static void Destructor(napi_env env, void *obj, void *)
        {
                ((Cam*)obj)->~Cam();
        }
};

void CamInit(napi_env env, napi_value exports)
{
        Cam::Init(env, exports);
}

} } // namespace cam::native
