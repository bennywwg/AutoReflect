cmake_minimum_required(VERSION 3.0.0)
project(AutoReflect VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)

# Variable holding Resources folder
set(RESOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Resources")
set(AR_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Include")

set(SourceFiles
    Source/Generator.cpp
)

set(HeaderFiles

)

function(DeclareExecutable ExeName AdditionalSourceFiles)
    add_executable(${ExeName}
        ${AdditionalSourceFiles}

        ${HeaderFiles}
    )

    target_include_directories(${ExeName} PUBLIC ${PROJECT_SOURCE_DIR}/Source/)
    target_include_directories(${ExeName} PUBLIC ${PROJECT_SOURCE_DIR}/glm/)
    target_include_directories(${ExeName} PUBLIC ${PROJECT_SOURCE_DIR}/json/include/)
    set_property(TARGET ${ExeName} PROPERTY CXX_STANDARD 20)

    # Set AR_RESOURCES_FOLDER Macro
    target_compile_definitions(${ExeName} PUBLIC AR_RESOURCES_DIR="${RESOURCES_DIR}")
    target_compile_definitions(${ExeName} PUBLIC AR_INCLUDE_DIR="${AR_INCLUDE_DIR}")
endfunction()

set(AutoReflectFiles Source/Generator.cpp)
DeclareExecutable(AutoReflect "${AutoReflectFiles}")