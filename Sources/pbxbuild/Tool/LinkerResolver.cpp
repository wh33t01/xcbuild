/**
 Copyright (c) 2015-present, Facebook, Inc.
 All rights reserved.

 This source code is licensed under the BSD-style license found in the
 LICENSE file in the root directory of this source tree. An additional grant
 of patent rights can be found in the PATENTS file in the same directory.
 */

#include <pbxbuild/Tool/LinkerResolver.h>
#include <pbxbuild/Tool/Environment.h>
#include <pbxbuild/Tool/Context.h>
#include <pbxbuild/Tool/OptionsResult.h>
#include <pbxbuild/Tool/Tokens.h>

namespace Tool = pbxbuild::Tool;
using libutil::FSUtil;

Tool::LinkerResolver::
LinkerResolver(pbxspec::PBX::Linker::shared_ptr const &linker) :
    _linker(linker)
{
}

Tool::LinkerResolver::
~LinkerResolver()
{
}

void Tool::LinkerResolver::
resolve(
    Tool::Context *toolContext,
    pbxsetting::Environment const &environment,
    std::vector<std::string> const &inputFiles,
    std::vector<Phase::File> const &inputLibraries,
    std::string const &output,
    std::vector<std::string> const &additionalArguments,
    std::string const &executable
)
{
    std::vector<std::string> special;
    std::vector<Tool::Invocation::AuxiliaryFile> auxiliaries;

    special.insert(special.end(), additionalArguments.begin(), additionalArguments.end());

    if (_linker->supportsInputFileList() || _linker->identifier() == Tool::LinkerResolver::LibtoolToolIdentifier()) {
        std::string path = environment.expand(pbxsetting::Value::Parse("$(LINK_FILE_LIST_$(variant)_$(arch))"));
        std::string contents;
        for (std::string const &input : inputFiles) {
            contents += input + "\n";
        }
        Tool::Invocation::AuxiliaryFile fileList = Tool::Invocation::AuxiliaryFile(path, contents, false);
        auxiliaries.push_back(fileList);
    }

    std::unordered_set<std::string> libraryPaths;
    std::unordered_set<std::string> frameworkPaths;
    for (Phase::File const &library : inputLibraries) {
        if (library.fileType()->isFrameworkWrapper()) {
            frameworkPaths.insert(FSUtil::GetDirectoryName(library.path()));
        } else {
            libraryPaths.insert(FSUtil::GetDirectoryName(library.path()));
        }
    }
    for (std::string const &libraryPath : libraryPaths) {
        special.push_back("-L" + libraryPath);
    }
    for (std::string const &libraryPath : frameworkPaths) {
        special.push_back("-F" + libraryPath);
    }
    special.push_back("-F" + environment.resolve("BUILT_PRODUCTS_DIR"));

    for (Phase::File const &library : inputLibraries) {
        std::string base = FSUtil::GetBaseNameWithoutExtension(library.path());
        if (library.fileType()->isFrameworkWrapper()) {
            special.push_back("-framework");
            special.push_back(base);
        } else {
            if (base.find("lib") == 0) {
                base = base.substr(3);
            }
            special.push_back("-l" + base);
        }
    }

    std::vector<Tool::Invocation::DependencyInfo> dependencyInfo;
    if (_linker->identifier() == Tool::LinkerResolver::LinkerToolIdentifier()) {
        auto info = Tool::Invocation::DependencyInfo(
            dependency::DependencyInfoFormat::Binary,
            environment.expand(_linker->dependencyInfoFile()));
        dependencyInfo.push_back(info);

        special.push_back("-Xlinker");
        special.push_back("-dependency_info");
        special.push_back("-Xlinker");
        special.push_back(info.path());
    }

    pbxspec::PBX::Tool::shared_ptr tool = std::static_pointer_cast <pbxspec::PBX::Tool> (_linker);
    Tool::Environment toolEnvironment = Tool::Environment::Create(tool, environment, toolContext->workingDirectory(), inputFiles, { output });
    Tool::OptionsResult options = Tool::OptionsResult::Create(toolEnvironment, toolContext->workingDirectory(), nullptr);
    Tool::Tokens::ToolExpansions tokens = Tool::Tokens::ExpandTool(toolEnvironment, options, executable, special);

    std::vector<std::string> arguments = tokens.arguments();
    if (_linker->identifier() == Tool::LinkerResolver::LipoToolIdentifier()) {
        // This is weird, but this flag is invalid yet is in the specification.
        arguments.erase(std::remove(arguments.begin(), arguments.end(), "-arch_only"), arguments.end());
    }

    Tool::Invocation invocation;
    invocation.executable() = tokens.executable();
    invocation.arguments() = arguments;
    invocation.environment() = options.environment();
    invocation.workingDirectory() = toolContext->workingDirectory();
    invocation.inputs() = toolEnvironment.inputs(toolContext->workingDirectory());
    invocation.outputs() = toolEnvironment.outputs(toolContext->workingDirectory());
    invocation.auxiliaryFiles() = auxiliaries;
    invocation.dependencyInfo() = dependencyInfo;
    invocation.logMessage() = tokens.logMessage();
    toolContext->invocations().push_back(invocation);
}

std::unique_ptr<Tool::LinkerResolver> Tool::LinkerResolver::
Create(Phase::Environment const &phaseEnvironment, std::string const &identifier)
{
    Build::Environment const &buildEnvironment = phaseEnvironment.buildEnvironment();
    Target::Environment const &targetEnvironment = phaseEnvironment.targetEnvironment();

    pbxspec::PBX::Linker::shared_ptr linker = buildEnvironment.specManager()->linker(identifier, targetEnvironment.specDomains());
    if (linker == nullptr) {
        fprintf(stderr, "warning: could not find linker %s\n", identifier.c_str());
        return nullptr;
    }

    return std::unique_ptr<Tool::LinkerResolver>(new Tool::LinkerResolver(linker));
}

