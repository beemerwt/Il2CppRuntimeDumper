#define NOMINMAX
#include <Windows.h>
#include <map>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <functional>
#include <cstdint>

// You can change this to the directory you want to dump the files to
#define DUMP_DIR "Il2CppDump"

// You can replace this with your own version of the Il2Cpp header
#include "DummyIl2Cpp.h"

#include "include/Il2CppRuntimeDumper.h"
#include "Il2CppFunctions.h"
#include "SafeCall.h"

#define FIELD_ATTRIBUTE_STATIC 0x0010
#define FIELD_ATTRIBUTE_LITERAL 0x0040
#define FIELD_ATTRIBUTE_NOT_SERIALIZED 0x0080
#define FIELD_ATTRIBUTE_HAS_FIELD_RVA 0x0100

// Domain functions
SafeCall<il2cpp_domain_get_fn> il2cpp_domain_get;
SafeCall<il2cpp_domain_get_assemblies_fn> il2cpp_domain_get_assemblies;

// Assembly functions
SafeCall<il2cpp_assembly_get_name_fn> il2cpp_assembly_get_name;
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

// Field functions
SafeCall<il2cpp_field_get_type_fn> il2cpp_field_get_type;
SafeCall<il2cpp_field_get_name_fn> il2cpp_field_get_name;
SafeCall<il2cpp_field_get_offset_fn> il2cpp_field_get_offset;

// Method functions
SafeCall<il2cpp_method_get_name_fn> il2cpp_method_get_name;
SafeCall<il2cpp_method_get_return_type_fn> il2cpp_method_get_return_type;
SafeCall<il2cpp_method_get_param_count_fn> il2cpp_method_get_param_count;
SafeCall<il2cpp_method_get_param_fn> il2cpp_method_get_param;

// Type functions
SafeCall<il2cpp_type_get_name_fn> il2cpp_type_get_name;
SafeCall<il2cpp_type_get_attrs_fn> il2cpp_type_get_attrs;

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
        // mov rax, imm64; jmp rax — some Unity builds may do this
        return *reinterpret_cast<void**>(next + 2);
    }

    // Unknown format
    return thunkFunc;
}

template<typename ReturnType, typename... Args>
bool ValidateFunction(HMODULE hModule, void* waitForActivationAddr, const char* name, SafeCall<ReturnType(*)(Args...)>* funcPtr) {
    void* funcAddr = GetProcAddress(hModule, name);
    if (!funcAddr) {
        std::cerr << "[Dumper] Failed to get address of " << name << "\n";
        return false;
    }

    funcAddr = ResolveIl2CppThunk(funcAddr, waitForActivationAddr);
    if (!funcAddr) {
        std::cerr << "[Dumper] Failed to resolve thunk for " << name << "\n";
        return false;
    }

    auto func = reinterpret_cast<ReturnType(*)(Args...)>(funcAddr);
    *funcPtr = func;
    return true;
}

#define VALIDATE_PTR(ptr) \
    if (ValidateFunction(gameAssembly, waitForActivationAddr, #ptr, &ptr)) \
    { \
        std::cout << "[Dumper] Got " #ptr " at " << std::hex << ptr.func << std::endl; \
    } else { \
        std::cout << "[Dumper] Failed to get " #ptr "\n"; \
        return false; \
    } \

bool ValidateFunctions(HMODULE gameAssembly) {
    // Initialize function pointers
    // Function pointers
    void* waitForActivationAddr = GetProcAddress(gameAssembly, "WaitForActivation");

    // Domain functions
    VALIDATE_PTR(il2cpp_domain_get);
    VALIDATE_PTR(il2cpp_domain_get_assemblies);

    // Assembly functions
    VALIDATE_PTR(il2cpp_assembly_get_name);
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
    VALIDATE_PTR(il2cpp_class_get_type);
    VALIDATE_PTR(il2cpp_class_get_image);
    VALIDATE_PTR(il2cpp_class_get_parent);

    // Field functions
    VALIDATE_PTR(il2cpp_field_get_type);
    VALIDATE_PTR(il2cpp_field_get_name);
    VALIDATE_PTR(il2cpp_field_get_offset);

    // Method functions
    VALIDATE_PTR(il2cpp_method_get_name);
    VALIDATE_PTR(il2cpp_method_get_return_type);
    VALIDATE_PTR(il2cpp_method_get_param_count);
    VALIDATE_PTR(il2cpp_method_get_param);

    // Type functions
    VALIDATE_PTR(il2cpp_type_get_name);
    VALIDATE_PTR(il2cpp_type_get_attrs);

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
        std::cout << "[Dumper] Failed to validate functions\n";
        return;
    }

    const Il2CppDomain* domain = il2cpp_domain_get();
    if (!domain) {
        std::cout << "[Dumper] il2cpp_domain_get returned nullptr";
        return;
    }

    size_t count = 0;
    const Il2CppAssembly** assemblies = il2cpp_domain_get_assemblies(domain, &count);
    if (!assemblies) {
        std::cout << "[Dumper] il2cpp_domain_get_assemblies returned nullptr";
        return;
    }

    if (count == 0) {
        std::cout << "[Dumper] il2cpp_domain_get_assemblies returned with 0 assemblies";
        return;
    }

    // Check for directory "Il2CppDump"
    if (!std::filesystem::exists(outputPath)) {
        std::cout << "[Dumper] Creating directory: " << outputPath << std::endl;
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

            DumpCppStyleClass(file, image, klass);
        }

        file.close();
    }
}

IL2RD_API void DumpIl2CppRuntime(HMODULE gameAssembly, const std::string& outputPath) {
    if (!gameAssembly) {
        std::cout << "[Dumper] Invalid game assembly handle\n";
        return;
    }

    if (outputPath.empty()) {
        std::cout << "[Dumper] Invalid output path\n";
        return;
    }

    // Initialize the Il2Cpp functions
    if (!ValidateFunctions(gameAssembly)) {
        std::cout << "[Dumper] Failed to validate functions\n";
        return;
    }

    // Dump all types
    DumpAllTypes(gameAssembly, outputPath);
}