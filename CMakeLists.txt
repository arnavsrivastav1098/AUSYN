cmake_minimum_required(VERSION 3.24)

project(Ausyn VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Concurrent Core Gui Network Sql Widgets)

qt_standard_project_setup()

qt_add_executable(ausyn WIN32
    assets/ausyn.qrc
    src/main.cpp
    src/ui/main_window.cpp
    src/ui/main_window_workload.cpp
    src/ui/main_window_companion.cpp
    src/ui/main_window_companion_checks.cpp
    src/ui/main_window.h
    src/ui/page_hub.cpp
    src/ui/page_hub.h
    src/ui/live_visuals.cpp
    src/ui/live_visuals.h
    src/ui/activity_page.cpp
    src/ui/activity_page.h
    src/ui/window_chrome.cpp
    src/ui/window_chrome.h
    src/ui/alert_policy.cpp
    src/ui/alert_policy.h
    src/ui/notification_popup.cpp
    src/ui/notification_popup.h
    src/ui/resource_inspector.cpp
    src/ui/resource_inspector.h
    src/monitoring/system_snapshot.h
    src/monitoring/background_relief.cpp
    src/monitoring/background_relief.h
    src/monitoring/system_collector.cpp
    src/monitoring/system_collector.h
    src/monitoring/telemetry_service.cpp
    src/monitoring/telemetry_service.h
    src/monitoring/hardware_change_tracker.cpp
    src/monitoring/hardware_change_tracker.h
    src/data/local_history_store.cpp
    src/data/local_history_store.h
    src/intelligence/insight_types.h
    src/intelligence/workload_coach.cpp
    src/intelligence/workload_coach.h
    src/intelligence/autonomous_companion.cpp
    src/intelligence/autonomous_companion.h
    src/intelligence/insight_engine.cpp
    src/intelligence/insight_engine.h
    src/ui/history_chart.cpp
    src/ui/history_chart.h
    src/ui/insight_list_widget.cpp
    src/ui/insight_list_widget.h
    src/assistant/assistant_engine.cpp
    src/assistant/assistant_engine.h
    src/assistant/assistant_client.cpp
    src/assistant/assistant_client.h
    src/settings/user_preferences.cpp
    src/settings/user_preferences.h
    src/intelligence/prediction_engine.cpp
    src/intelligence/prediction_engine.h
    src/intelligence/gaming_readiness.cpp
    src/intelligence/gaming_readiness.h
    src/intelligence/game_catalog.cpp
    src/intelligence/game_catalog.h
    src/ui/prediction_page.cpp
    src/ui/prediction_page.h
    src/ui/gaming_page.cpp
    src/ui/gaming_page.h
    src/ui/battery_page.cpp
    src/ui/battery_page.h
    src/intelligence/event_log_types.h
    src/monitoring/event_log_collector.cpp
    src/monitoring/event_log_collector.h
    src/ui/event_log_page.cpp
    src/ui/event_log_page.h
    src/intelligence/app_inventory_types.h
    src/monitoring/app_inventory_collector.cpp
    src/monitoring/app_inventory_collector.h
    src/monitoring/software_change_tracker.cpp
    src/monitoring/software_change_tracker.h
    src/ui/app_inventory_page.cpp
    src/ui/app_inventory_page.h
    src/intelligence/security_status_types.h
    src/monitoring/security_status_collector.cpp
    src/monitoring/security_status_collector.h
    src/ui/security_page.cpp
    src/ui/security_page.h
    src/monitoring/snapshot_validator.cpp
    src/monitoring/snapshot_validator.h
    src/ui/data_quality_page.cpp
    src/ui/data_quality_page.h
    src/ui/agent_health_page.cpp
    src/ui/agent_health_page.h
    src/ui/network_page.cpp
    src/ui/network_page.h
    src/ui/proactive_briefing_widget.cpp
    src/ui/proactive_briefing_widget.h
    src/ui/troubleshooting_page.cpp
    src/ui/troubleshooting_page.h
)

target_link_libraries(ausyn PRIVATE
    Qt6::Concurrent
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Sql
    Qt6::Widgets
)

if(WIN32)
    target_sources(ausyn PRIVATE assets/ausyn.rc)
    target_include_directories(ausyn PRIVATE assets)
    target_link_libraries(ausyn PRIVATE Advapi32 Crypt32 Dxgi Iphlpapi Ole32 OleAut32 Pdh PowrProf Psapi Shell32 Wevtapi Wscapi wbemuuid)
    target_compile_definitions(ausyn PRIVATE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
endif()

target_compile_definitions(ausyn PRIVATE
    AUSYN_VERSION="${PROJECT_VERSION}"
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
)

if(MSVC)
    target_compile_options(ausyn PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(ausyn PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS ausyn
    RUNTIME DESTINATION .
    BUNDLE DESTINATION .
)
