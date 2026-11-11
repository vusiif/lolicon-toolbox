add_library(MojibakeLogic STATIC EncodingConverter.h EncodingConverter.cpp
    MojibakeCandidate.h CandidateScorer.h CandidateScorer.cpp
    CandidateGenerator.h CandidateGenerator.cpp MojibakeAnalyzer.h MojibakeAnalyzer.cpp
    ByteEncodingAnalyzer.h ByteEncodingAnalyzer.cpp RecoveryOperation.h RecoveryOperation.cpp
    FilenameRecovery.h FilenameRecovery.cpp LossyGbkRecovery.h LossyGbkRecovery.cpp)
target_include_directories(MojibakeLogic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Qt6 REQUIRED COMPONENTS Concurrent)
target_link_libraries(MojibakeLogic PUBLIC Qt6::Core Qt6::Concurrent)
# Word model for lossy GBK recovery, derived from the jieba dictionary (MIT).
qt_add_resources(MojibakeLogic charlm PREFIX "/charlm" BASE "${PROJECT_SOURCE_DIR}/resources/charlm"
    FILES "${PROJECT_SOURCE_DIR}/resources/charlm/zh_wordlm.bin")
add_library(ClipboardSourceLogic STATIC ClipboardSource.h ClipboardSource.cpp)
target_include_directories(ClipboardSourceLogic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ClipboardSourceLogic PUBLIC Qt6::Gui)
add_library(RecoveryReporting STATIC DiagnosticReport.h DiagnosticReport.cpp)
target_include_directories(RecoveryReporting PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(RecoveryReporting PUBLIC MojibakeLogic ClipboardSourceLogic)
add_library(ToolMojibake STATIC MojibakePage.h MojibakePage.cpp TextFileRecoveryPage.h TextFileRecoveryPage.cpp
    FilenameRecoveryPage.h FilenameRecoveryPage.cpp ClipboardSourcePage.h ClipboardSourcePage.cpp
    DiagnosticReportWidget.h DiagnosticReportWidget.cpp RecoveryWorkspace.h RecoveryWorkspace.cpp MojibakeAi.h MojibakeAi.cpp)
target_include_directories(ToolMojibake PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ToolMojibake PUBLIC RecoveryReporting ToolboxUI Qt6::Concurrent)
