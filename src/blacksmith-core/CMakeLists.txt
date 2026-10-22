add_library(
  blacksmith-core STATIC
  include/domain/models.hpp
  src/domain/models.cpp
  include/domain/transformations.hpp
  include/battle-platform/standard-platform.hpp
  src/battle-platform/standard-platform.cpp
  include/skill-system/skill-sets.hpp
  include/skill-system/dsl-macro.hpp
  include/skill-system/prior.hpp
  src/skill-system/common.cpp
  src/skill-system/actions.cpp
  src/skill-system/warlock/warlock.cpp
  src/skill-system/warlock/alchemy.cpp
  src/skill-system/cannon.cpp
  src/skill-system/driver.cpp
  src/skill-system/lancer.cpp
  src/skill-system/bloodsigil.cpp
  # Temporarily disabled professions; keep their implementations for restoration.
  # src/skill-system/monk.cpp
  # src/skill-system/prophet.cpp
  src/skill-system/holybook.cpp
  src/skill-system/phantombook/phantombook.cpp
  src/skill-system/phantombook/nightmare.cpp
  src/skill-system/crossbow.cpp
  src/skill-system/cauldron/cauldron.cpp
  src/skill-system/cauldron/elementalarmor.cpp)

target_compile_features(blacksmith-core PUBLIC cxx_std_20)

target_include_directories(
  blacksmith-core
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

set_project_warnings(blacksmith-core)
