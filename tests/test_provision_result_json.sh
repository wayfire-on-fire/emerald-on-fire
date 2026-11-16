#!/usr/bin/env bash
# test_provision_result_json.sh — provision.sh writes a result JSON via an
# UNQUOTED heredoc (<<JSON), so the shell expands the interpolated values
# ($DISTRO_ID, $PRETTY, $SESSION_USER, ...). Pre-fix those values were NOT
# JSON-escaped: a value containing a double quote, backslash, or control char
# would produce MALFORMED JSON (the result is parsed back by run-kvm.sh).
# The fix adds a json_escape() function and wraps each interpolated value in
# it, so the emitted JSON is always well-formed.
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)/.."
PROV="$ROOT/e2e/distros/provision.sh"
[ -f "$PROV" ] || { echo "FAIL: provision.sh not found"; exit 1; }

fail=0

# 1. A json_escape function must exist.
if grep -qE '^json_escape\(\)' "$PROV"; then
    echo "ok: a json_escape() function is defined"
else
    echo "FAIL: no json_escape() function"; fail=1
fi

# 2. The result-JSON heredoc must escape its interpolated values. The
#    "pretty" line is a canary — a distro pretty name is the most likely
#    value to contain special characters.
if grep -qE '"pretty":[[:space:]]*"\$\(json_escape "\$PRETTY"\)"' "$PROV"; then
    echo "ok: the result JSON escapes its interpolated values"
else
    echo "FAIL: the result JSON does not escape its interpolated values"; fail=1
fi

# 3. Behavioral: json_escape must escape a double quote and a backslash.
#    Extract the function body and run it on a hostile value.
fn_body="$(sed -n '/^json_escape()/,/^}/p' "$PROV")"
[ -n "$fn_body" ] || { echo "FAIL: could not extract json_escape()"; exit 1; }
eval "$fn_body"
out="$(json_escape 'a"b\c')"
# In JSON, a"b\c must become a\"b\\c
if [ "$out" = 'a\"b\\c' ]; then
    echo "ok: json_escape escapes quotes and backslashes (got: $out)"
else
    echo "FAIL: json_escape did not escape correctly (got: '$out')"; fail=1
fi

# 4. Behavioral: json_escape must escape tab, newline, and carriage return.
tab_out="$(json_escape "$(printf 'a\tb')")"
if [ "$tab_out" = 'a\tb' ]; then
    echo "ok: json_escape escapes a tab"
else
    echo "FAIL: json_escape did not escape a tab (got: '$tab_out')"; fail=1
fi
nl_out="$(json_escape "$(printf 'a\nb')")"
if [ "$nl_out" = 'a\nb' ]; then
    echo "ok: json_escape escapes a newline"
else
    echo "FAIL: json_escape did not escape a newline (got: '$nl_out')"; fail=1
fi
cr_out="$(json_escape "$(printf 'a\rb')")"
if [ "$cr_out" = 'a\rb' ]; then
    echo "ok: json_escape escapes a carriage return"
else
    echo "FAIL: json_escape did not escape a CR (got: '$cr_out')"; fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: provision.sh escapes its result-JSON values"
