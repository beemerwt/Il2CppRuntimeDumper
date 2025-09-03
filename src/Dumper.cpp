#define NOMINMAX
#include <Windows.h>
#include <map>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <thread>
#include <functional>
#include <cstdint>
#include <mutex>

#define IL2CPP_TYPE_END        0   // End of List
#define IL2CPP_TYPE_VOID       1   // System.Void
#define IL2CPP_TYPE_BOOLEAN    2   // System.Boolean
#define IL2CPP_TYPE_CHAR       3   // System.Char

// Signed integrals
#define IL2CPP_TYPE_I1   4   // System.SByte
#define IL2CPP_TYPE_I2   6   // System.Int16
#define IL2CPP_TYPE_I4   8   // System.Int32
#define IL2CPP_TYPE_I8   10  // System.Int64

// Unsigned integrals
#define IL2CPP_TYPE_U1   5   // System.Byte
#define IL2CPP_TYPE_U2   7   // System.UInt16
#define IL2CPP_TYPE_U4   9   // System.UInt32
#define IL2CPP_TYPE_U8   11  // System.UInt64

// then I1..U8 as above
#define IL2CPP_TYPE_R4        12   // System.Single
#define IL2CPP_TYPE_R8        13   // System.Double
#define IL2CPP_TYPE_STRING    14   // System.String

// You can change this to the directory you want to dump the files to
#define DUMP_DIR "Il2CppDump"

// You can replace this with your own version of the Il2Cpp header
#include "DummyIl2Cpp.hpp"

#include "Il2CppRuntimeDumper.hpp"
#include "Il2CppFunctions.hpp"
#include "SafeCall.hpp"

#define FIELD_ATTRIBUTE_STATIC 0x0010
#define FIELD_ATTRIBUTE_LITERAL 0x0040
#define FIELD_ATTRIBUTE_NOT_SERIALIZED 0x0080
#define FIELD_ATTRIBUTE_HAS_FIELD_RVA 0x0100

struct EnumFieldTracker {
    std::string enumName;
    std::string fieldName;
    Il2CppClass* klass;
    size_t offset;
    std::string baseType;
};

std::ostringstream oss;
std::vector<EnumFieldTracker> unresolvedEnums;
std::mutex enumMutex;

void DumpLog() {
    OutputDebugStringA(oss.str().c_str());
    oss.str("");
}

// Domain functions
SafeCall<il2cpp_domain_get_fn> il2cpp_domain_get;
SafeCall<il2cpp_domain_get_assemblies_fn> il2cpp_domain_get_assemblies;

// Assembly functions
SafeCall<il2cpp_assembly_get_image_fn> il2cpp_assembly_get_image;

// Image functions
SafeCall<il2cpp_image_get_name_fn> il2cpp_image_get_name;
SafeCall<il2cpp_image_get_class_fn> il2cpp_image_get_class;
SafeCall<il2cpp_image_get_class_count_fn> il2cpp_image_get_class_count;

// Class functions
SafeCall<il2cpp_class_get_name_fn> il2cpp_class_get_name;
SafeCall<il2cpp_class_get_fields_fn> il2cpp_class_get_fields;
SafeCall<il2cpp_class_get_methods_fn> il2cpp_class_get_methods;
SafeCall<il2cpp_class_get_namespace_fn> il2cpp_class_get_namespace;
SafeCall<il2cpp_class_from_type_fn> il2cpp_class_from_type;
SafeCall<il2cpp_class_get_type_fn> il2cpp_class_get_type;
SafeCall<il2cpp_class_get_image_fn> il2cpp_class_get_image;
SafeCall<il2cpp_class_get_parent_fn> il2cpp_class_get_parent;
SafeCall<il2cpp_class_get_element_class_fn> il2cpp_class_get_element_class;

// Enum Class functions
SafeCall<il2cpp_class_is_enum_fn> il2cpp_class_is_enum;
SafeCall<il2cpp_class_enum_basetype_fn> il2cpp_class_enum_basetype;

// Field functions
SafeCall<il2cpp_field_get_type_fn> il2cpp_field_get_type;
SafeCall<il2cpp_field_get_name_fn> il2cpp_field_get_name;
SafeCall<il2cpp_field_get_offset_fn> il2cpp_field_get_offset;
SafeCall<il2cpp_field_static_get_value_fn> il2cpp_field_static_get_value;
SafeCall<il2cpp_field_get_flags_fn> il2cpp_field_get_flags;
SafeCall<il2cpp_field_get_default_value_fn> il2cpp_field_get_default_value;
SafeCall<il2cpp_field_get_value_object_fn> il2cpp_field_get_value_object;

// Method functions
SafeCall<il2cpp_method_get_name_fn> il2cpp_method_get_name;
SafeCall<il2cpp_method_get_return_type_fn> il2cpp_method_get_return_type;
SafeCall<il2cpp_method_get_param_count_fn> il2cpp_method_get_param_count;
SafeCall<il2cpp_method_get_param_fn> il2cpp_method_get_param;

// Type functions
SafeCall<il2cpp_type_get_name_fn> il2cpp_type_get_name;
SafeCall<il2cpp_type_get_attrs_fn> il2cpp_type_get_attrs;
SafeCall<il2cpp_type_get_type_fn> il2cpp_type_get_type;

// Object functions
SafeCall<il2cpp_object_unbox_fn> il2cpp_object_unbox;

// Runtime functions
SafeCall<il2cpp_runtime_class_init_fn> il2cpp_runtime_class_init;

enum class Visibility : uint8_t {
    None = 0,
    Private = 0x01,
    ProtectedInternal = 0x02,
    Internal = 0x03,
    Protected = 0x04,
    Public = 0x06,
};

enum class FieldFlags : uint8_t {
    None = 0,
    Static = 1 << 0,
    Literal = 1 << 2,
    NotSerialized = 1 << 3,
    HasFieldRVA = 1 << 4, // initialized static
};

struct StringType {
    uint32_t length;
    const char* chars;
};

struct FieldType {
    FieldFlags flags;
    Visibility visibility;
    std::string typeName;
    std::string name;
    int32_t offset;
};

void* ResolveIl2CppThunk(void* thunkFunc, void* waitforActivationAddr) {
    if (!thunkFunc)
        return nullptr;

    if (!waitforActivationAddr)
        return thunkFunc;

    uint8_t* code = static_cast<uint8_t*>(thunkFunc);

    // Step 1: Check for call to WaitForActivation (0xE8 relative call)
    if (code[0] != 0xE8) {
        // Not a call instruction
        return thunkFunc;
    }

    // Decode call target
    int32_t callOffset = *reinterpret_cast<int32_t*>(code + 1);
    void* calledFunc = code + 5 + callOffset;

    if (calledFunc != waitforActivationAddr) {
        // Doesn't call WaitForActivation
        return thunkFunc;
    }

    // Step 2: Expect a JMP or CALL instruction next
    uint8_t* next = code + 5;

    if (next[0] == 0xE9) {
        // JMP rel32
        int32_t rel = *reinterpret_cast<int32_t*>(next + 1);
        return next + 5 + rel;
    } else if (next[0] == 0xFF && next[1] == 0x25) {
        // JMP [rip+offset]
        int32_t ripOffset = *reinterpret_cast<int32_t*>(next + 2);
        uint8_t** indirect = reinterpret_cast<uint8_t**>(next + 6 + ripOffset);
        return *indirect;
    } else if (next[0] == 0x48 && next[1] == 0xB8) {
        // mov rax, imm64; jmp rax � some Unity builds may do this
        return *reinterpret_cast<void**>(next + 2);
    }

    // Unknown format
    return thunkFunc;
}

template<typename ReturnType, typename... Args>
bool ValidateFunction(HMODULE hModule, void* waitForActivationAddr, const char* name, SafeCall<ReturnType(*)(Args...)>* funcPtr) {
    void* funcAddr = reinterpret_cast<void*>(GetProcAddress(hModule, name));
    if (funcAddr == nullptr) {
        oss << "[Dumper] Failed to get address of " << name << "\n";
        DumpLog();
        return false;
    }

    funcAddr = ResolveIl2CppThunk(reinterpret_cast<void*>(funcAddr), waitForActivationAddr);
    if (!funcAddr) {
        oss << "[Dumper] Failed to resolve thunk for " << name << "\n";
        DumpLog();
        return false;
    }

    auto func = reinterpret_cast<ReturnType(*)(Args...)>(funcAddr);
    *funcPtr = func;
    return true;
}

#define VALIDATE_PTR(ptr) \
    if (ValidateFunction(gameAssembly, waitForActivationAddr, #ptr, &ptr)) \
    { \
        oss << "[Dumper] Got " #ptr " at " << std::hex << ptr.func << std::endl; \
        DumpLog(); \
    } else { \
        oss << "[Dumper] Failed to get " #ptr "\n"; \
        DumpLog(); \
        return false; \
    } \

bool ValidateFunctions(HMODULE gameAssembly) {
    // Initialize function pointers
    // Function pointers
    void* waitForActivationAddr = reinterpret_cast<void*>(GetProcAddress(gameAssembly, "WaitForActivation"));

    // Domain functions
    VALIDATE_PTR(il2cpp_domain_get);
    VALIDATE_PTR(il2cpp_domain_get_assemblies);

    // Assembly functions
    VALIDATE_PTR(il2cpp_assembly_get_image);

    // Image functions
    VALIDATE_PTR(il2cpp_image_get_name);
    VALIDATE_PTR(il2cpp_image_get_class);
    VALIDATE_PTR(il2cpp_image_get_class_count);

    // Class functions
    VALIDATE_PTR(il2cpp_class_get_name);
    VALIDATE_PTR(il2cpp_class_get_fields);
    VALIDATE_PTR(il2cpp_class_get_methods);
    VALIDATE_PTR(il2cpp_class_get_namespace);
    VALIDATE_PTR(il2cpp_class_from_type);
    VALIDATE_PTR(il2cpp_class_get_type);
    VALIDATE_PTR(il2cpp_class_get_image);
    VALIDATE_PTR(il2cpp_class_get_parent);
    VALIDATE_PTR(il2cpp_class_get_element_class);

    // Enum class functions
    VALIDATE_PTR(il2cpp_class_is_enum);
    VALIDATE_PTR(il2cpp_class_enum_basetype);

    // Field functions
    VALIDATE_PTR(il2cpp_field_get_type);
    VALIDATE_PTR(il2cpp_field_get_name);
    VALIDATE_PTR(il2cpp_field_get_offset);
    VALIDATE_PTR(il2cpp_field_static_get_value);
    VALIDATE_PTR(il2cpp_field_get_flags);
    VALIDATE_PTR(il2cpp_field_get_default_value);

    // Method functions
    VALIDATE_PTR(il2cpp_method_get_name);
    VALIDATE_PTR(il2cpp_method_get_return_type);
    VALIDATE_PTR(il2cpp_method_get_param_count);
    VALIDATE_PTR(il2cpp_method_get_param);

    // Type functions
    VALIDATE_PTR(il2cpp_type_get_name);
    VALIDATE_PTR(il2cpp_type_get_attrs);
    VALIDATE_PTR(il2cpp_type_get_type);

    // Object functions
    VALIDATE_PTR(il2cpp_object_unbox);

    // Runtime functions
    VALIDATE_PTR(il2cpp_runtime_class_init);

    return true;
}

std::string to_hex(int value) {
    std::stringstream ss;
    ss << std::hex << value;
    return ss.str();
}

std::string HexOffset(int offset) {
    if (offset == -1)
        return "// static";
    std::stringstream ss;
    ss << "0x" << std::hex << offset;
    return ss.str();
}

struct DumpField {
    std::string typeName;
    std::string fieldName;
    std::string comment;
    bool isStatic;
};

std::map<std::string, std::string> typeMap = {
    {"System.String", "Il2CppString*"},
    {"System.Object", "Il2CppObject"},
    {"System.IntPtr", "void*"},
    {"System.UIntPtr", "void*"},
    {"System.Char", "wchar_t"},
    {"System.Boolean", "bool"},
    {"System.Single", "float"},
    {"System.Double", "double"},
    {"System.Byte", "uint8_t"},
    {"System.SByte", "int8_t"},
    {"System.Int8", "int8_t"},
    {"System.UInt8", "uint8_t"},
    {"System.Int16", "int16_t"},
    {"System.UInt16", "uint16_t"},
    {"System.Int32", "int32_t"},
    {"System.UInt32", "uint32_t"},
    {"System.Int64", "int64_t"},
    {"System.UInt64", "uint64_t"},
    {"System.Void", "void"},
    {"System.Collections.Generic.List", "List"},
    {"System.Collections.Generic.Dictionary", "Dictionary"}
};

static bool EnumUnderlyingWidth(Il2CppClass* enumKlass, size_t* outSize) {
    const Il2CppType* bt = il2cpp_class_enum_basetype(enumKlass);
    if (!bt) return false;
    switch (il2cpp_type_get_type(bt)) {
        case IL2CPP_TYPE_I1: case IL2CPP_TYPE_U1: *outSize = 1; return true;
        case IL2CPP_TYPE_I2: case IL2CPP_TYPE_U2: *outSize = 2; return true;
        case IL2CPP_TYPE_I4: case IL2CPP_TYPE_U4: *outSize = 4; return true;
        case IL2CPP_TYPE_I8: case IL2CPP_TYPE_U8: *outSize = 8; return true;
        default: return false;
    }
}

static bool GetEnumLiteralI64(const FieldInfo* field, Il2CppClass* enumKlass, int64_t* out) {
    // Must be a static literal field
    uint32_t flags = il2cpp_field_get_flags(field);
    if ((flags & FIELD_ATTRIBUTE_STATIC) == 0 || (flags & FIELD_ATTRIBUTE_LITERAL) == 0)
        return false;

    Il2CppObject* boxed = il2cpp_field_get_value_object(field, NULL); // static -> obj = NULL
    if (!boxed) return false;

    void* raw = il2cpp_object_unbox(boxed);
    if (!raw) return false;

    size_t w = 0;
    if (!EnumUnderlyingWidth(enumKlass, &w)) w = 4; // most enums default to I4

    switch (w) {
        case 1: *out = *reinterpret_cast<const int8_t*>(raw);  return true;
        case 2: *out = *reinterpret_cast<const int16_t*>(raw); return true;
        case 4: *out = *reinterpret_cast<const int32_t*>(raw); return true;
        case 8: *out = *reinterpret_cast<const int64_t*>(raw); return true;
        default: return false;
    }
}

void ReplaceStringTypes(std::string& typeName) {
    // replace any string from the typeMap keys with the value
    for (const auto& pair : typeMap) {
        size_t pos = 0;
        while ((pos = typeName.find(pair.first, pos)) != std::string::npos) {
            // Ensure that the next character is not the opening square bracket
            if (pos + pair.first.length() < typeName.length() && typeName[pos + pair.first.length()] == '[') {
                pos += pair.first.length();
                continue;
            }

            typeName.replace(pos, pair.first.length(), pair.second);
            pos += pair.second.length(); // advance past the replacement
        }
    }
}

void EnumResolverThread(const std::string& outputPath) {
    using namespace std::chrono_literals;
    const int maxRetries = 100;
    int retries = 0;

    auto numEnums = unresolvedEnums.size();
    OutputDebugStringA(("[Dumper] Starting Enum Resolver Thread for " + std::to_string(numEnums) + " enums.\n").c_str());

    std::map<std::string, std::map<std::string, int64_t>> resolved;
    bool isEmpty = unresolvedEnums.empty();

    while (isEmpty) {
        {
            std::lock_guard<std::mutex> lock(enumMutex);
            for (auto it = unresolvedEnums.begin(); it != unresolvedEnums.end();) {
                Il2CppClass* klass = it->klass;
                if (!klass) { it = unresolvedEnums.erase(it); continue; }

                // You already gated on enums when pushing, but be defensive:
                if (!il2cpp_class_is_enum(klass)) { it = unresolvedEnums.erase(it); continue; }

                // Make sure class statics are initialized (safe no-op for literal enums)
                il2cpp_runtime_class_init(klass);

                // Find the matching static literal field by name
                void* iter = NULL;
                const FieldInfo* f = NULL;
                const FieldInfo* match = NULL;

                while ((f = il2cpp_class_get_fields(klass, &iter))) {
                    const char* fname = il2cpp_field_get_name(f);
                    if (!fname || it->fieldName != fname) continue;

                    uint32_t flags = il2cpp_field_get_flags(f);
                    if ((flags & FIELD_ATTRIBUTE_STATIC) == 0) continue;
                    if ((flags & FIELD_ATTRIBUTE_LITERAL) == 0) continue; // enum members should be literal

                    match = f;
                    break;
                }

                if (!match) { ++it; continue; }

                int64_t value = 0;
                if (GetEnumLiteralI64(match, it->klass, &value)) {
                    resolved[it->enumName][it->fieldName] = value;
                    oss << "[Dumper] Resolved enum " << it->enumName << "::" << it->fieldName
                        << " = " << value << " (offset: " << it->offset << ", base type: " << it->baseType << ")\n";
                    DumpLog();

                    it = unresolvedEnums.erase(it); // remove from unresolved list
                    isEmpty = unresolvedEnums.empty();
                } else {
                    // couldn’t read yet; try again later
                }
            }
        }

        std::this_thread::sleep_for(100ms);
    }

    // Output to file
    std::filesystem::path dumpPath = outputPath / std::filesystem::path("EnumValuesResolved.txt");
    std::ofstream out(dumpPath);
    for (const auto& [enumName, fields] : resolved) {
        out << "enum class " << enumName << " {\n";
        for (const auto& [fieldName, value] : fields) {
            out << "    " << fieldName << " = " << value << ",\n";
        }
        out << "};\n\n";
    }
}

void DumpEnumClass(std::ofstream& file, const Il2CppImage* image, Il2CppClass* klass, HMODULE gameAssembly) {
    std::string namespaceName = il2cpp_class_get_namespace(klass);
    std::string enumName = il2cpp_class_get_name(klass);
    std::string imageName = il2cpp_image_get_name(image);
    const Il2CppType* baseType = il2cpp_class_enum_basetype(klass);

    std::string baseTypeStr = baseType ? il2cpp_type_get_name(baseType) : "int";
    file << "enum class " << namespaceName << ( namespaceName.empty() ? "" : ".") << enumName << " : " << baseTypeStr << " {\n";

    // Defensive fallback (rare, but some odd versions/mods)
    if (!baseType) {
        Il2CppClass* elem = il2cpp_class_get_element_class(klass);
        if (elem) baseType = il2cpp_class_get_type(elem);
    }

    void* fieldIter = nullptr;
    FieldInfo* field = nullptr;

    while ((field = il2cpp_class_get_fields(klass, &fieldIter)) != nullptr) {
        unsigned int attrs = il2cpp_type_get_attrs(baseType);
        if (!(attrs & FIELD_ATTRIBUTE_STATIC))
            continue;

        const char* name = il2cpp_field_get_name(field);

        //Il2CppObject* value = nullptr;
        int value = 0;
        il2cpp_field_static_get_value(field, &value);
        file << "    " << name << " = " << value << ",\n";
    }

    file << "};\n";
}

void DumpCppStyleClass(std::ofstream& file, const Il2CppImage* image, Il2CppClass* klass) {
    auto ClassToString = [](Il2CppClass* klass) {
        auto type = il2cpp_class_get_type(klass);
        if (!type) return "UnknownClass";

        auto typeName = il2cpp_type_get_name(type);
        return typeName;
    };

    Il2CppClass* base = il2cpp_class_get_parent(klass);
    const char* className = ClassToString(klass);

    file << "\n// From: " << il2cpp_image_get_name(image) << "\n";

    if (base && base != klass) {
        const char* _baseName = ClassToString(base);
        auto hasName = typeMap.find(_baseName);
        std::string baseName = hasName != typeMap.end() ? hasName->second : _baseName;

        file << "struct " << className << " : " << baseName << " {\n";
    } else {
        file << "struct " << className << " : Il2CppObject {\n";
    }

    // Fields
    void* fieldIter = nullptr;

    std::vector<DumpField> fields;
    while (const FieldInfo* field = il2cpp_class_get_fields(klass, &fieldIter)) {
        if (!field) continue;

        const Il2CppType* fieldType = il2cpp_field_get_type(field);
        if (!fieldType) continue;

        const char* fieldName = il2cpp_field_get_name(field);
        if (!fieldName) continue;

        const char* fieldTypeName = il2cpp_type_get_name(fieldType);
        if (!fieldTypeName) continue;

        unsigned int attrs = il2cpp_type_get_attrs(fieldType);
        int offset = il2cpp_field_get_offset(field);

        std::string typeName = std::string(fieldTypeName);
        ReplaceStringTypes(typeName);

        fields.push_back(DumpField{
            typeName,
            fieldName,
            HexOffset(offset),
            (attrs & FIELD_ATTRIBUTE_STATIC) != 0
        });
    }

    int numStaticFields = 0;
    for (const auto& field : fields) {
        if (!field.isStatic)
            continue;
        file << "    static " << field.typeName << " " << field.fieldName << ";\n";
        numStaticFields++;
    }

    if (numStaticFields > 0)
        file << "\n";

    size_t longestLine = 0;
    for (const auto& field : fields) {
        if (field.isStatic)
            continue;

        std::string line = "    " + field.typeName + " " + field.fieldName + ";";
        longestLine = std::max(longestLine, line.length());
    }

    int numInstanceFields = 0;
    for (const auto& field : fields) {
        if (field.isStatic)
            continue;
        std::string line = "    " + field.typeName + " " + field.fieldName + ";";
        file << line << std::string(longestLine - line.length() + 1, ' ')
            << " // " << field.comment << "\n";
        numInstanceFields++;
    }

    if (numInstanceFields > 0)
        file << "\n";

    // Methods
    void* methodIter = nullptr;
    while (const MethodInfo* method = il2cpp_class_get_methods(klass, &methodIter)) {
        if (!method)
            continue;
        
        const char* methodName = il2cpp_method_get_name(method);
        if (!methodName)
            continue;

        const Il2CppType* returnType = il2cpp_method_get_return_type(method);
        if (!returnType)
            continue;

        const char* returnTypeName = il2cpp_type_get_name(returnType);
        if (!returnTypeName)
            continue;

        std::string returnTypeStr = std::string(returnTypeName);
        //ReplaceStringTypes(returnTypeStr);

        file << "    " << returnType << " " << methodName << "(";

        unsigned char paramCount = il2cpp_method_get_param_count(method);

        for (int i = 0; i < paramCount; ++i) {
            const Il2CppType* params = il2cpp_method_get_param(method, i);
            if (!params)
                continue;

            const char* paramTypeName = il2cpp_type_get_name(params);
            if (!paramTypeName)
                continue;

            std::string paramTypeNameStr(paramTypeName);
            // ReplaceStringTypes(paramTypeNameStr);

            file << paramTypeNameStr.c_str();
            if (i < paramCount - 1)
                file << ", ";
        }

        file << ");\n";
    }

    file << "};\n";
}


void DumpAllTypes(HMODULE gameAssembly, const std::string& outputPath) {
    if (!ValidateFunctions(gameAssembly)) {
        oss << "[Dumper] Failed to validate functions\n";
        DumpLog();
        return;
    }

    const Il2CppDomain* domain = il2cpp_domain_get();
    if (!domain) {
        oss << "[Dumper] il2cpp_domain_get returned nullptr";
        DumpLog();
        return;
    }

    size_t count = 0;
    const Il2CppAssembly** assemblies = il2cpp_domain_get_assemblies(domain, &count);
    if (!assemblies) {
        OutputDebugStringA("[Dumper] il2cpp_domain_get_assemblies returned nullptr");
        return;
    }

    if (count == 0) {
        OutputDebugStringA("[Dumper] il2cpp_domain_get_assemblies returned with 0 assemblies");
        return;
    }

    // Check for directory "Il2CppDump"
    if (!std::filesystem::exists(outputPath)) {
        oss << "[Dumper] Creating directory: " << outputPath << std::endl;
        DumpLog();
        std::filesystem::create_directory(outputPath);
    }

    for (size_t i = 0; i < count; i++) {
        const Il2CppAssembly* assembly = assemblies[i];
        if (!assembly) continue;

        const Il2CppImage* image = il2cpp_assembly_get_image(assembly);
        if (!image) continue;

        const char* imageName = il2cpp_image_get_name(image);
        if (!imageName) continue;

        std::filesystem::path dumpPath = outputPath / std::filesystem::path(std::string(imageName) + ".txt");
        std::ofstream file(dumpPath);

        uint32_t totalClassCount = static_cast<uint32_t>(il2cpp_image_get_class_count(image));
        for (uint32_t j = 0; j < totalClassCount; j++) {
            Il2CppClass* klass = il2cpp_image_get_class(image, j);
            if (!klass)  continue;

            if (il2cpp_class_is_enum(klass))
                DumpEnumClass(file, image, klass, gameAssembly);
            else 
                DumpCppStyleClass(file, image, klass);
        }

        file.close();
    }
}

IL2RD_API void DumpIl2CppRuntime(HMODULE gameAssembly, const char* outputPath) {
    if (!gameAssembly) {
        OutputDebugStringA("[Dumper] Invalid game assembly handle\n");
        return;
    }

    if (!outputPath || !*outputPath) {
        OutputDebugStringA("[Dumper] Invalid output path\n");
        return;
    }

    std::string out(outputPath);

    // Initialize the Il2Cpp functions
    if (!ValidateFunctions(gameAssembly)) {
        OutputDebugStringA("[Dumper] Failed to validate functions\n");
        return;
    }

    // Dump all types
    DumpAllTypes(gameAssembly, out);

    std::thread resolverThread(EnumResolverThread, out);
    resolverThread.detach(); // Or store the handle if you want to `join` later
}