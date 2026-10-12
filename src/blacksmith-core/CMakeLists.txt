add_library(blacksmith-core STATIC
    
    include/domain/models.hpp

    include/skill-system/professions.hpp
    include/skill-system/dsl-macro.hpp
    include/domain/transformations.hpp

    src/domain/models.cpp

    src/skill-system/common.cpp
    src/skill-system/warlock.cpp
    src/skill-system/cannon.cpp
    
    src/skill-system/driver.cpp
    src/skill-system/lancer.cpp
    src/skill-system/alchemy.cpp
    include/battle-platform/standard-platform.hpp
    src/battle-platform/standard-platform.cpp
    src/skill-system/bloodsigil.cpp
    include/skill-system/prior.hpp
)

target_compile_features(blacksmith-core PUBLIC cxx_std_20)

target_include_directories(blacksmith-core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_project_warnings(blacksmith-core)
