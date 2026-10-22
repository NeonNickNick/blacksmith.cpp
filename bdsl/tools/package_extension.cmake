# Package the extension with CMake's ZIP support; no Python/npm packages required.
cmake_minimum_required(VERSION 3.21)
foreach(required IN ITEMS BDSL_SOURCE BDSL_COMPILER BDSL_STAGE BDSL_TARGET BDSL_OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required; use the bdsl-vsix build target")
    endif()
endforeach()
if(NOT BDSL_TARGET MATCHES "^(win32|linux|darwin)-(x64|arm64)$")
    message(FATAL_ERROR "Unsupported VSIX target: ${BDSL_TARGET}")
endif()
file(READ "${BDSL_SOURCE}/vscode/package.json" manifest)
foreach(field IN ITEMS name version publisher displayName description)
    string(JSON ${field} GET "${manifest}" "${field}")
endforeach()
string(JSON engine GET "${manifest}" engines vscode)
execute_process(COMMAND "${BDSL_COMPILER}" --version
    OUTPUT_VARIABLE compiler_version ERROR_VARIABLE compiler_error
    OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE result TIMEOUT 10)
if(NOT "${result}" STREQUAL "0" OR NOT compiler_version STREQUAL "bdslc ${version}")
    message(FATAL_ERROR "Compiler/extension version mismatch: ${compiler_version}\n${compiler_error}")
endif()

function(xml_escape variable)
    set(value "${${variable}}")
    string(REPLACE "&" "&amp;" value "${value}")
    string(REPLACE "<" "&lt;" value "${value}")
    string(REPLACE ">" "&gt;" value "${value}")
    string(REPLACE "\"" "&quot;" value "${value}")
    string(REPLACE "'" "&apos;" value "${value}")
    set(${variable} "${value}" PARENT_SCOPE)
endfunction()
foreach(field IN ITEMS name version publisher displayName description engine)
    xml_escape(${field})
endforeach()
set(xml [=[<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011" xmlns:d="http://schemas.microsoft.com/developer/vsx-schema-design/2011">
  <Metadata>
    <Identity Language="en-US" Id="@name@" Version="@version@" Publisher="@publisher@" TargetPlatform="@BDSL_TARGET@" />
    <DisplayName>@displayName@</DisplayName>
    <Description xml:space="preserve">@description@</Description>
    <Tags>bdsl,blacksmith,dsl</Tags><Categories>Programming Languages,Formatters,Themes</Categories>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="@engine@" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionKind" Value="workspace" />
      <Property Id="Microsoft.VisualStudio.Code.ExecutesCode" Value="true" />
    </Properties>
  </Metadata>
  <Installation><InstallationTarget Id="Microsoft.VisualStudio.Code" /></Installation>
  <Dependencies />
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Content.Details" Path="extension/README.md" Addressable="true" />
  </Assets>
</PackageManifest>
]=])
string(CONFIGURE "${xml}" xml @ONLY)
file(MAKE_DIRECTORY "${BDSL_STAGE}/extension/docs" "${BDSL_STAGE}/extension/bin")
file(WRITE "${BDSL_STAGE}/extension.vsixmanifest" "${xml}")
file(WRITE "${BDSL_STAGE}/[Content_Types].xml" [=[<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="json" ContentType="application/json" />
  <Default Extension="js" ContentType="application/javascript" />
  <Default Extension="md" ContentType="text/markdown" />
  <Default Extension="exe" ContentType="application/octet-stream" />
  <Default Extension="vsixmanifest" ContentType="text/xml" />
</Types>
]=])
set(files extension.vsixmanifest "[Content_Types].xml")
set(sources package.json README.md language-configuration.json)
foreach(folder IN ITEMS src syntaxes snippets themes)
    file(GLOB children RELATIVE "${BDSL_SOURCE}/vscode" "${BDSL_SOURCE}/vscode/${folder}/*")
    list(APPEND sources ${children})
endforeach()
list(SORT sources)
foreach(source IN LISTS sources)
    if(IS_DIRECTORY "${BDSL_SOURCE}/vscode/${source}")
        message(FATAL_ERROR "Expected extension file: ${source}")
    endif()
    get_filename_component(parent "${source}" DIRECTORY)
    file(COPY "${BDSL_SOURCE}/vscode/${source}" DESTINATION "${BDSL_STAGE}/extension/${parent}")
    list(APPEND files "extension/${source}")
endforeach()
file(COPY "${BDSL_SOURCE}/docs/SYNTAX.md" DESTINATION "${BDSL_STAGE}/extension/docs")
if(BDSL_TARGET MATCHES "^win32-")
    set(binary bdslc.exe)
else()
    set(binary bdslc)
endif()
configure_file("${BDSL_COMPILER}" "${BDSL_STAGE}/extension/bin/${binary}" COPYONLY)
list(APPEND files "extension/docs/SYNTAX.md" "extension/bin/${binary}")
get_filename_component(output_dir "${BDSL_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
# Running from the staging folder keeps every archive path relative on CMake 3.21.
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${BDSL_OUTPUT}" --format=zip -- ${files}
    WORKING_DIRECTORY "${BDSL_STAGE}" RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "VSIX archive creation failed: ${error}")
endif()
message(STATUS "VSIX: ${BDSL_OUTPUT}")
