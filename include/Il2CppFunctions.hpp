#pragma once
#include "DummyIl2Cpp.hpp"
#include <cstddef>

// Domain functions
typedef Il2CppDomain* (*il2cpp_domain_get_fn)();
typedef const Il2CppAssembly** (*il2cpp_domain_get_assemblies_fn)(const Il2CppDomain* domain, size_t* size);

// Assembly functions
typedef const Il2CppImage* (*il2cpp_assembly_get_image_fn)(const Il2CppAssembly* assembly);

// Image functions
typedef const char* (*il2cpp_image_get_name_fn)(const Il2CppImage* image);
typedef Il2CppClass* (*il2cpp_image_get_class_fn)(const Il2CppImage* image, size_t index);
typedef size_t(*il2cpp_image_get_class_count_fn)(const Il2CppImage* image);

// Class functions
typedef const char* (*il2cpp_class_get_name_fn)(Il2CppClass* klass);
typedef FieldInfo* (*il2cpp_class_get_fields_fn)(Il2CppClass* klass, void** iter);
typedef const MethodInfo* (*il2cpp_class_get_methods_fn)(Il2CppClass* klass, void** iter);
typedef const char* (*il2cpp_class_get_namespace_fn)(Il2CppClass* klass);
typedef Il2CppClass* (*il2cpp_class_from_type_fn)(const Il2CppType* type);
typedef const Il2CppType* (*il2cpp_class_get_type_fn)(Il2CppClass* klass);
typedef const Il2CppImage* (*il2cpp_class_get_image_fn)(Il2CppClass* klass);
typedef Il2CppClass* (*il2cpp_class_get_parent_fn)(Il2CppClass* klass);
typedef Il2CppClass* (*il2cpp_class_get_element_class_fn)(Il2CppClass* klass);

// Enum Class functions
using il2cpp_class_is_enum_fn = bool(*)(const Il2CppClass*);
using il2cpp_class_enum_basetype_fn = const Il2CppType*(*)(Il2CppClass*);

// Field functions
typedef const Il2CppType* (*il2cpp_field_get_type_fn)(const FieldInfo* field);
typedef const char* (*il2cpp_field_get_name_fn)(const FieldInfo* field);
typedef int (*il2cpp_field_get_offset_fn)(const FieldInfo* field);
typedef void (*il2cpp_field_static_get_value_fn)(FieldInfo*, void*);
typedef unsigned int (*il2cpp_field_get_flags_fn)(const FieldInfo* field);
typedef Il2CppObject* (*il2cpp_field_get_default_value_fn)(const FieldInfo* field);
typedef Il2CppObject* (*il2cpp_field_get_value_object_fn)(const FieldInfo* field, Il2CppObject* obj);

// Method functions
typedef const char* (*il2cpp_method_get_name_fn)(const MethodInfo* method);
typedef const Il2CppType* (*il2cpp_method_get_return_type_fn)(const MethodInfo* method);
typedef unsigned char (*il2cpp_method_get_param_count_fn)(const MethodInfo* method);
typedef const Il2CppType* (*il2cpp_method_get_param_fn)(const MethodInfo* method, unsigned char index);

// Type functions
typedef const char* (*il2cpp_type_get_name_fn)(const Il2CppType* type);
typedef unsigned int (*il2cpp_type_get_attrs_fn)(const Il2CppType* type);
typedef int (*il2cpp_type_get_type_fn)(const Il2CppType* type);

// Object functions
typedef void* (*il2cpp_object_unbox_fn)(Il2CppObject* obj);

// Runtime functions
typedef void (*il2cpp_runtime_class_init_fn)(const Il2CppClass* klass);