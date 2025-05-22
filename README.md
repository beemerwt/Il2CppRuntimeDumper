# Il2CppRuntimeDumper
Dumps a text file for every assembly included in an IL2CPP application. These text files list all the types, methods, fields, and properties in the assembly. The text files are named after the assembly name and are saved in the same directory as the executable.

I chose to release this in a repository so it could be used to help anyone looking to reverse Il2Cpp Unity games. It was originally tooled for Il2Cpp version 2021.3.43f1, but I have since modified it to retain as much functionality as possible for other versions, although your mileage may vary.