file(GLOB_RECURSE FILE_SOURCES "*.h" "*.c" "*.cpp")

add_library(EffectLib STATIC ${FILE_SOURCES})

target_link_libraries(EffectLib 
	lzo2
)

GroupSourcesByFolder(EffectLib)
