#include <cam/assembler.h>
#include <cam/memory.h>

#include <node_api.h>

#include <stdlib.h>
#include <assert.h>
#include <memory>
#include <fstream>

using namespace std;

#define DECLARE_NAPI_METHOD(name, func) { name, 0, func, 0, 0, 0, napi_default, 0 }

namespace cam { namespace native {

static void write_to_file(void *ud, void *buf, int bytes)
{
        auto os = (ofstream*)ud;
        os->write((const char*)buf, bytes);
}

static bool is_undefined(napi_env env, napi_value v)
{
        napi_valuetype t;
        napi_status status = napi_typeof(env, v, &t);
        assert(status == napi_ok);
        return t == napi_undefined;
}

class Assembler
{
private:
        Assembler(napi_env env)
                : _env(env)
                , _wrapper(nullptr)
                , _as(nullptr)
                , _alloc(nullptr)
        {
                cam_error_t ec;
                _alloc = (struct cam_alloc_s*)malloc(cam_malloc_sizeof());
                ec = cam_malloc_init(_alloc);
                assert(ec == CEC_SUCCESS);
        }

       ~Assembler()
        {
                if (_as) {
                        cam_asm_drop(_as);
                        cam_mem_free(_alloc, _as);
                }

                int leaked_bytes = cam_malloc_drop(_alloc);
                assert(leaked_bytes == 0);
        }

        static napi_value New(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 2;
                napi_value jsthis, argv[2];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 2);

                size_t str_len, copied_len;
                status = napi_get_value_string_utf8(env, argv[0], nullptr, 0, &str_len);
                assert(status == napi_ok);
                shared_ptr<char> module(new char[str_len + 1]);
                status = napi_get_value_string_utf8(env, argv[0], module.get(), str_len + 1, &copied_len);
                assert(status == napi_ok && str_len == copied_len);

                uint8_t *uuid;
                size_t uuid_length;
                status = napi_get_buffer_info(env, argv[1], (void**)&uuid, &uuid_length);
                assert(status == napi_ok && uuid_length == 16);

                Assembler *obj = new Assembler(env);
                status = napi_wrap(env, jsthis, (void*)obj, &Assembler::Destructor, nullptr, &obj->_wrapper);
                assert(status == napi_ok);

                cam_error_t ec;
                assert(obj->_as == nullptr);
                obj->_as = (struct cam_asm_s*)cam_mem_alloc(obj->_alloc, cam_asm_sizeof(), 4);
                ec = cam_asm_init(obj->_as, obj->_alloc, module.get(), uuid);
                assert(ec == CEC_SUCCESS);

                return jsthis;
        }

        static napi_value Serialize(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 1;
                napi_value jsthis, argv[1];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 1);

                Assembler *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                size_t str_len, copied_len;
                status = napi_get_value_string_utf8(env, argv[0], nullptr, 0, &str_len);
                assert(status == napi_ok);
                shared_ptr<char> path(new char[str_len + 1]);
                status = napi_get_value_string_utf8(env, argv[0], path.get(), str_len + 1, &copied_len);
                assert(status == napi_ok && str_len == copied_len);

                ofstream os;
                os.open(path.get(), fstream::out | fstream::binary | fstream::trunc);
                cam_asm_serialize(obj->_as, &write_to_file, &os);
                os.close();

                return nullptr;
        }

        static napi_value WfieldComp2(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 1;
                napi_value jsthis, argv[1];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 1);

                Assembler *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                double value;
                status = napi_get_value_double(env, argv[0], &value);
                assert(status == napi_ok);

                napi_value ret;
                status = napi_create_int32(env, cam_asm_wfield_comp_2(obj->_as, value), &ret);
                assert(status == napi_ok);
                return ret;
        }

        static napi_value WfieldComp4(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 1;
                napi_value jsthis, argv[1];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 1);

                Assembler *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                napi_value c4v;

                bool is_signed;
                status = napi_get_named_property(env, argv[0], "isSigned", &c4v);
                assert(status == napi_ok);
                status = napi_get_value_bool(env, c4v, &is_signed);
                assert(status == napi_ok);

                int scale;
                status = napi_get_named_property(env, argv[0], "scale", &c4v);
                assert(status == napi_ok);
                status = napi_get_value_int32(env, c4v, &scale);
                assert(status == napi_ok);

                bool lossless;
                cam_comp_4_t value;
                status = napi_get_named_property(env, argv[0], "value", &c4v);
                assert(status == napi_ok);
                status = napi_get_value_bigint_int64(env, c4v, &value, &lossless);
                assert(status == napi_ok && lossless);

                const int idx = cam_asm_wfield_comp_4(obj->_as, is_signed, scale, value);

                napi_value ret;
                status = napi_create_int32(env, idx, &ret);
                assert(status == napi_ok);
                return ret;
        }

        static napi_value WfieldDisplay(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 1;
                napi_value jsthis, argv[1];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok);

                Assembler *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                int idx;

                if (argc == 1 && !is_undefined(env, argv[0])) {
                        size_t str_len, copied_len;
                        status = napi_get_value_string_utf8(env, argv[0], nullptr, 0, &str_len);
                        assert(status == napi_ok);
                        shared_ptr<char> value(new char[str_len + 1]);
                        status = napi_get_value_string_utf8(env, argv[0], value.get(), str_len + 1, &copied_len);
                        assert(status == napi_ok && str_len == copied_len);
                        idx = cam_asm_wfield_display(obj->_as, value.get());
                } else {
                        idx = cam_asm_wfield_display(obj->_as, nullptr);
                }

                napi_value ret;
                status = napi_create_int32(env, idx, &ret);
                assert(status == napi_ok);
                return ret;
        }

        static napi_value Import(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 2;
                napi_value jsthis, argv[2];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 2);

                Assembler *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                size_t str_len, copied_len;

                status = napi_get_value_string_utf8(env, argv[0], nullptr, 0, &str_len);
                assert(status == napi_ok);
                shared_ptr<char> module(new char[str_len + 1]);
                status = napi_get_value_string_utf8(env, argv[0], module.get(), str_len + 1, &copied_len);
                assert(status == napi_ok && str_len == copied_len);

                status = napi_get_value_string_utf8(env, argv[1], nullptr, 0, &str_len);
                assert(status == napi_ok);
                shared_ptr<char> program(new char[str_len + 1]);
                status = napi_get_value_string_utf8(env, argv[1], program.get(), str_len + 1, &copied_len);
                assert(status == napi_ok && str_len == copied_len);

                const int idx = cam_asm_import(obj->_as, module.get(), program.get());

                napi_value ret;
                status = napi_create_int32(env, idx, &ret);
                assert(status == napi_ok);
                return ret;
        }

        static napi_value EmitA(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 1;
                napi_value jsthis, argv[1];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 1);

                Assembler *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                int opcode;
                status = napi_get_value_int32(env, argv[0], &opcode);
                assert(status == napi_ok);

                const int idx = cam_asm_emit_a(obj->_as, (uint8_t)opcode);

                napi_value ret;
                status = napi_create_int32(env, idx, &ret);
                assert(status == napi_ok);
                return ret;
        }

        static napi_value EmitB(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 2;
                napi_value jsthis, argv[2];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 2);

                Assembler *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                int opcode;
                status = napi_get_value_int32(env, argv[0], &opcode);
                assert(status == napi_ok);

                int b0;
                status = napi_get_value_int32(env, argv[1], &b0);
                assert(status == napi_ok);

                const int idx = cam_asm_emit_b(obj->_as, (uint8_t)opcode, b0);

                napi_value ret;
                status = napi_create_int32(env, idx, &ret);
                assert(status == napi_ok);
                return ret;
        }

        static napi_value EmitC(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 3;
                napi_value jsthis, argv[3];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok && argc == 3);

                Assembler *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                int opcode;
                status = napi_get_value_int32(env, argv[0], &opcode);
                assert(status == napi_ok);

                int c0;
                status = napi_get_value_int32(env, argv[1], &c0);
                assert(status == napi_ok);

                int c1;
                status = napi_get_value_int32(env, argv[2], &c1);
                assert(status == napi_ok);

                const int idx = cam_asm_emit_c(obj->_as, (uint8_t)opcode, (int8_t)c0, (int8_t)c1);

                napi_value ret;
                status = napi_create_int32(env, idx, &ret);
                assert(status == napi_ok);
                return ret;
        }

        static napi_value PrototypePush(napi_env env, napi_callback_info info)
        {
                napi_status status;

                size_t argc = 1;
                napi_value jsthis, argv[1];
                status = napi_get_cb_info(env, info, &argc, argv, &jsthis, nullptr);
                assert(status == napi_ok);

                Assembler *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                int idx;
                if (argc == 1 && !is_undefined(env, argv[0])) {
                        size_t str_len, copied_len;
                        status = napi_get_value_string_utf8(env, argv[0], nullptr, 0, &str_len);
                        assert(status == napi_ok);
                        shared_ptr<char> name(new char[str_len + 1]);
                        status = napi_get_value_string_utf8(env, argv[0], name.get(), str_len + 1, &copied_len);
                        assert(status == napi_ok && str_len == copied_len);
                        idx = cam_asm_prototype_push(obj->_as, name.get());
                } else {
                        idx = cam_asm_prototype_push(obj->_as, nullptr);
                }

                napi_value ret;
                status = napi_create_int32(env, idx, &ret);
                assert(status == napi_ok);
                return ret;
        }

        static napi_value PrototypePop(napi_env env, napi_callback_info info)
        {
                napi_status status;

                napi_value jsthis;
                status = napi_get_cb_info(env, info, nullptr, nullptr, &jsthis, nullptr);
                assert(status == napi_ok);

                Assembler *obj;
                status = napi_unwrap(env, jsthis, (void**)&obj);
                assert(status == napi_ok);

                cam_asm_prototype_pop(obj->_as);

                return nullptr;
        }

        napi_env _env;
        napi_ref _wrapper;
        struct cam_asm_s *_as;
        struct cam_alloc_s *_alloc;

public:
        static void Init(napi_env env, napi_value exports)
        {
                napi_status status;

                const napi_property_descriptor props[] = {
                        DECLARE_NAPI_METHOD("serialize",     &Serialize),
                        DECLARE_NAPI_METHOD("wfieldComp2",   &WfieldComp2),
                        DECLARE_NAPI_METHOD("wfieldComp4",   &WfieldComp4),
                        DECLARE_NAPI_METHOD("wfieldDisplay", &WfieldDisplay),
                        DECLARE_NAPI_METHOD("import",        &Import),
                        DECLARE_NAPI_METHOD("emitA",         &EmitA),
                        DECLARE_NAPI_METHOD("emitB",         &EmitB),
                        DECLARE_NAPI_METHOD("emitC",         &EmitC),
                        DECLARE_NAPI_METHOD("prototypePush", &PrototypePush),
                        DECLARE_NAPI_METHOD("prototypePop",  &PrototypePop)
                };

                const size_t num_props = sizeof(props) / sizeof(props[0]);

                napi_value cons;
                status = napi_define_class(
                        env, "AssemblerNative", NAPI_AUTO_LENGTH, &New, nullptr, num_props, props, &cons);
                assert(status == napi_ok);

                status = napi_set_named_property(env, exports, "AssemblerNative", cons);
                assert(status == napi_ok);
        }

        static void Destructor(napi_env env, void *obj, void *)
        {
                ((Assembler*)obj)->~Assembler();
        }
};


void AssemblerInit(napi_env env, napi_value exports)
{
        Assembler::Init(env, exports);
}

} } // namespace cam::native
