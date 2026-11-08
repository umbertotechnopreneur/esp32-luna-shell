"""Display-only English labels for genuine hook tool identities."""

from __future__ import annotations

import sys
from typing import Any


MAX_TOOL_LABEL_LENGTH = 192
MAX_TOOL_ID_LENGTH = 256

# Exact native IDs and their documented functions namespace are supported. An
# unrelated server ending in the same name does not inherit a native label.
_NATIVE_LABELS = {
    "apply_patch": "Edit files",
    "exec_command": "Run command",
    "write_stdin": "Command session",
    "shell_command": "Run shell command",
    "Bash": "Run shell command",
    "view_image": "View image",
    "request_user_input": "Ask a question",
    "request_user_input_async": "Ask a question",
    "list_mcp_resources": "List connected resources",
    "list_mcp_resource_templates": "List resource templates",
    "read_mcp_resource": "Read connected resource",
    "create_goal": "Create task goal",
    "get_goal": "Read task goal",
    "update_goal": "Update task goal",
}

_EXACT_LABELS = {
    "functions.exec": "Run tool script",
    "functions.wait": "Wait for tool result",
    "clock.sleep": "Wait",
    "clock.curr_time": "Read current time",
    "clock__curr_time": "Read current time",
    "web.run": "Web lookup",
    "web__run": "Web lookup",
    "image_gen.imagegen": "Create or edit image",
    "image_gen__imagegen": "Create or edit image",
    "collaboration.spawn_agent": "Start helper agent",
    "collaboration.send_message": "Message helper agent",
    "collaboration.followup_task": "Assign agent follow-up",
    "collaboration.wait_agent": "Wait for helper agent",
    "collaboration.list_agents": "Read agent status",
    "collaboration.interrupt_agent": "Interrupt helper agent",
    "mcp__codex_apps__github_fetch_file": "GitHub: read file",
    "mcp__codex_apps__github_fetch_blob": "GitHub: read blob",
    "mcp__codex_apps__github_fetch_commit": "GitHub: read commit",
    "mcp__codex_apps__github_fetch_issue": "GitHub: read issue",
    "mcp__codex_apps__github_fetch_issue_comments": "GitHub: read issue comments",
    "mcp__codex_apps__github_fetch_pr": "GitHub: read pull request",
    "mcp__codex_apps__github_get_pr_info": "GitHub: read pull request",
    "mcp__codex_apps__github_fetch_pr_comments": "GitHub: read pull request comments",
    "mcp__codex_apps__github_get_pr_diff": "GitHub: read pull request changes",
    "mcp__codex_apps__github_fetch_pr_patch": "GitHub: read pull request patch",
    "mcp__codex_apps__github_fetch_pr_file_patch": "GitHub: read file patch",
    "mcp__codex_apps__github_fetch_workflow_job_logs": "GitHub: read workflow logs",
    "mcp__codex_apps__github_fetch_workflow_job_steps": "GitHub: read workflow steps",
    "mcp__codex_apps__github_fetch_commit_workflow_runs": "GitHub: read commit checks",
    "mcp__codex_apps__github_get_commit_combined_status": "GitHub: read commit status",
    "mcp__codex_apps__github_fetch_workflow_run_jobs": "GitHub: read workflow jobs",
    "mcp__codex_apps__github_get_repo": "GitHub: read repository",
    "mcp__codex_apps__github_search_prs": "GitHub: search pull requests",
    "mcp__codex_apps__github_list_pr_changed_filenames": "GitHub: list changed files",
    "mcp__codex_apps__github_create_pull_request": "GitHub: create pull request",
    "mcp__codex_apps__github_update_pull_request": "GitHub: update pull request",
    "mcp__codex_apps__github_merge_pull_request": "GitHub: merge pull request",
    "mcp__codex_apps__github_add_review_to_pr": "GitHub: add pull request review",
    "mcp__codex_apps__github_add_comment_to_issue": "GitHub: add issue comment",
    "mcp__codex_apps__github_rerun_failed_workflow_run_jobs": "GitHub: rerun failed jobs",
    "mcp__codex_apps__google_drive_fetch": "Google Drive: read item",
    "mcp__codex_apps__google_drive_get_document": "Google Drive: read document",
    "mcp__codex_apps__google_drive_get_spreadsheet_range": "Google Drive: read sheet range",
    "mcp__codex_apps__gmail_read_email": "Gmail: read email",
    "mcp__codex_apps__gmail_read_email_thread": "Gmail: read email conversation",
    "mcp__codex_apps__search_service_web_run": "Web lookup",
    "mcp__ccp__ccp_ask_question": "Cody: ask a question",
    "mcp__ccp__ccp_audio_status": "Cody: read audio status",
    "mcp__ccp__ccp_cancel_request": "Cody: cancel request",
    "mcp__ccp__ccp_configure_clock": "Cody: configure clock",
    "mcp__ccp__ccp_connection_status": "Cody: read connection status",
    "mcp__ccp__ccp_debug_screen": "Cody: configure debug screen",
    "mcp__ccp__ccp_discover_devices": "Cody: find devices",
    "mcp__ccp__ccp_factory_reset_device": "Cody: reset settings",
    "mcp__ccp__ccp_list_sessions": "Cody: list chats",
    "mcp__ccp__ccp_pair_device": "Cody: pair device",
    "mcp__ccp__ccp_poll_request": "Cody: check reply",
    "mcp__ccp__ccp_publish_notification": "Cody: send notification",
    "mcp__ccp__ccp_read_clock": "Cody: read clock settings",
    "mcp__ccp__ccp_read_name": "Cody: read preferred name",
    "mcp__ccp__ccp_report_progress": "Cody: report progress",
    "mcp__ccp__ccp_reset_device": "Cody: restart device",
    "mcp__ccp__ccp_set_name": "Cody: set preferred name",
    "mcp__node_repl__js": "JavaScript: run code",
    "mcp__node_repl__js_reset": "JavaScript: reset session",
    "mcp__node_repl__js_add_node_module_dir": "JavaScript: add module directory",
}

# These spellings come from the exposed codex_apps tool metadata. Provider names
# are recognized only after the exact codex_apps server delimiter, never guessed
# from a substring in another server's name or a tool's arguments.
_APP_PROVIDERS = {
    "chatgpt_space": "ChatGPT Space",
    "clickup": "ClickUp",
    "codex_document_control": "Document control",
    "github": "GitHub",
    "gmail": "Gmail",
    "google_calendar": "Google Calendar",
    "google_drive": "Google Drive",
    "hotline": "Hotline",
    "microsoft_outlook_email": "Outlook",
    "pets": "Pets",
    "plugin_management": "Plugins",
    "safety_settings": "Safety settings",
    "search_service": "Search service",
    "sites": "Sites",
    "spotify": "Spotify",
}

_MCP_SERVERS = {
    "agentinbox": "AgentInbox",
    "ccp": "Cody",
    "node_repl": "JavaScript",
    "dataAnalyticsWidgets": "Data analytics",
}


# identity: Validated complete printable-ASCII identity or one parsed component.
# Returns its full readable spelling without guessing an action or removing words.
def _words(identity: str) -> str:
    return " ".join(identity.replace("_", " ").split())


# tool_name: Complete validated hook identity, with no command arguments or chat text.
# Returns an exact authored label or the complete readable server/method spelling.
def _display_name(tool_name: str) -> str:
    if tool_name in _EXACT_LABELS:
        return _EXACT_LABELS[tool_name]
    if tool_name in _NATIVE_LABELS:
        return _NATIVE_LABELS[tool_name]
    if tool_name.startswith("functions.") and tool_name[10:] in _NATIVE_LABELS:
        return _NATIVE_LABELS[tool_name[10:]]
    if tool_name.startswith("mcp__"):
        parts = tool_name.split("__", 2)
        if len(parts) == 3 and parts[1] and parts[2]:
            server, method = parts[1:]
            if server == "codex_apps":
                for identity, label in _APP_PROVIDERS.items():
                    prefix = identity + "_"
                    if method.startswith(prefix) and method[len(prefix):]:
                        return label + ": " + _words(method[len(prefix):])
            return _MCP_SERVERS.get(server, _words(server)) + ": " + _words(method)
    return _words(tool_name)


# tool_name: Genuine PreToolUse identity; this projection never replaces its correlation ID or phase.
# Raises ValueError for missing, oversized or non-printable-ASCII source identities.
# Returns at most 192 printable ASCII characters including Tool:, without truncating an identity.
def tool_display_label(tool_name: Any) -> str:
    if (not isinstance(tool_name, str) or not tool_name or len(tool_name) > MAX_TOOL_ID_LENGTH
            or any(not 32 <= ord(character) <= 126 for character in tool_name)):
        raise ValueError("Hook tool_name must be bounded printable ASCII")
    name = _display_name(tool_name)
    if not name:
        print("CCP tool label unavailable: source identity has no readable characters", file=sys.stderr, flush=True)
        return "Tool: name unavailable"
    label = "Tool: " + name
    if len(label) > MAX_TOOL_LABEL_LENGTH:
        print("CCP tool label unavailable: complete label exceeds 192 characters", file=sys.stderr, flush=True)
        return "Tool: name exceeds display limit"
    return label
