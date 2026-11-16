// test_themer_source.cpp — source-structure test for the themer's theme
// import (M23, issue #1).
//
// themer/main.c is a GTK TU that cannot be linked headlessly, so the
// import_theme() contract is verified textually: the function must
// (1) derive the theme name from the BASENAME (not the full path),
// (2) reject names that escape the themes dir, (3) validate the archive
// members BEFORE extracting (no absolute paths, no '..', no symlinks),
// and (4) extract to a temp dir and rename it into place.
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "test-paths.h"

namespace
{
std::string read_file(const std::string & path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The body of `import_theme(...)` (bracket-matched from the first '{'
// after the signature), with // comments stripped (main.c is /* */
// style, but the stripping keeps position checks comment-proof).
std::string fn_body(const std::string & src, const std::string & sig)
{
    const size_t at = src.find(sig);
    if (at == std::string::npos)
    {
        return {};
    }

    const size_t open = src.find('{', at);
    if (open == std::string::npos)
    {
        return {};
    }

    int depth  = 0;
    size_t end = open;
    for (size_t i = open; i < src.size(); i++)
    {
        if (src[i] == '{')
        {
            depth++;
        } else if (src[i] == '}')
        {
            depth--;
            if (depth == 0)
            {
                end = i;
                break;
            }
        }
    }

    std::string body = src.substr(open + 1, end - open - 1);
    std::string out;
    for (size_t i = 0; i < body.size(); i++)
    {
        if ((body[i] == '/') && (i + 1 < body.size()) && (body[i + 1] == '/'))
        {
            while (i < body.size() && body[i] != '\n')
            {
                i++;
            }

            if (i < body.size())
            {
                out += '\n';
            }

            continue;
        }

        out += body[i];
    }

    return out;
}

// M23 (issue #1): the theme import was a tar-slip. The file chooser
// hands import_theme() an ABSOLUTE path; the old code derived the
// theme name from the last '/' of that path, so the extraction target
// was `~/.emerald/themes/<full-path-minus-suffix>/` — '..' components
// escaped the themes dir, and the unvalidated tar members (absolute
// paths, '../', symlinks) wrote anywhere the user can write. The
// Import button is exactly where untrusted files enter.
TEST(ThemerSource, ImportThemeIsTarSlipSafe)
{
    const std::string src =
        read_file(std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/themer/main.c");
    ASSERT_FALSE(src.empty()) << "themer/main.c must be readable";

    const std::string body = fn_body(src, "static gchar * import_theme(");
    ASSERT_FALSE(body.empty()) << "could not locate import_theme()";

    // (1) The name comes from the BASENAME, not the full path.
    EXPECT_NE(body.find("g_path_get_basename"), std::string::npos) <<
        "import_theme() must derive the theme name from the file's "
        "basename (M23) — pre-fix it used g_strrstr on the absolute "
        "path, so the name (and the extraction target) contained the "
        "whole path";

    // (2) Names that escape the themes dir are rejected.
    EXPECT_NE(body.find("\"..\""), std::string::npos) <<
        "import_theme() must reject '..' names (M23) — pre-fix a "
        "path with '..' components extracted outside the themes dir";

    // (3) The archive is validated BEFORE extraction: the member list
    // (tar -tzf) is checked for absolute paths / '..' components, and
    // the verbose list (tar -tvzf) for member TYPE — a theme is
    // regular files ('-') and directories ('d') only; symlinks,
    // hardlinks, devices, FIFOs and any other special entry are
    // rejected (ChatGPT follow-up: a hardlink entry is not identified
    // by 'l' and has different semantics than a regular file).
    EXPECT_NE(body.find("tar -tzf"), std::string::npos) <<
        "import_theme() must list the archive members (tar -tzf) "
        "before extracting (M23) — pre-fix it went straight to "
        "tar -xzf with no validation";
    EXPECT_NE(body.find("'-'"), std::string::npos) <<
        "the type check must allow regular files ('-') (M23) — a "
        "theme is PNGs + an ini; nothing else is a feature";
    EXPECT_NE(body.find("'d'"), std::string::npos) <<
        "the type check must allow directories ('d') (M23) — and "
        "reject symlinks / hardlinks / devices / FIFOs";

    // (4) Extraction goes to a TEMP dir, then g_rename into place —
    // a failed/half extraction must not leave a broken theme in the
    // themes list, and --no-absolute-names strips absolute member
    // paths explicitly.
    EXPECT_NE(body.find("g_mkdtemp"), std::string::npos) <<
        "import_theme() must extract into a temp dir (g_mkdtemp) "
        "(M23) — pre-fix it extracted directly into the themes dir";
    EXPECT_NE(body.find("g_rename"), std::string::npos) <<
        "import_theme() must rename the temp dir into place "
        "(M23) — the move is atomic on the same filesystem";
    EXPECT_NE(body.find("--no-absolute-names"), std::string::npos) <<
        "the extraction must pass --no-absolute-names to tar (M23) "
        "— a second, explicit tar-slip defense";

    // (5) Re-import of an existing theme must not lose the old one:
    // rm-then-rename is not atomic (ChatGPT follow-up) — the swap goes
    // via a unique backup dir that is restored if the rename fails.
    // And an existing destination that is a symlink (or any
    // non-directory) must not be passed to a recursive delete.
    EXPECT_NE(body.find("-bak-XXXXXX"), std::string::npos) <<
        "re-import must swap via a backup dir (M23, ChatGPT "
        "follow-up) — pre-fix the rm-then-rename could lose the "
        "previous theme on a failed install";
    EXPECT_NE(body.find("G_FILE_TEST_IS_SYMLINK"), std::string::npos) <<
        "an existing destination must be validated WITHOUT "
        "following symlinks before any recursive delete (M23, "
        "ChatGPT follow-up)";

    // The direct-extraction anti-pattern must be gone: the old code
    // ran `tar -xzf <file> -C ~/.emerald/themes/<name>/` with no temp
    // dir and no validation.
    EXPECT_EQ(body.find("tar -xzf %s -C %s\""), std::string::npos) <<
        "import_theme() must not run the unvalidated "
        "tar -xzf directly (M23)";
}

// M24 (issue #1): the -i (import-from-CLI) flow had a use-after-destroy.
// create_filechooserdialog1() ran the dialog MODALLY (gtk_dialog_run),
// destroyed it, and then RETURNED the destroyed widget pointer; main()
// called gtk_widget_show() on that dangling pointer - a g_critical on
// every CLI run. The dialog is fully managed inside the function, so
// there is nothing for the caller to show: the function must return a
// success flag (not a widget), and main must not show a result.
TEST(ThemerSource, ImportCliDialogIsNotUsedAfterDestroy)
{
    const std::string src =
        read_file(std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/themer/main.c");
    ASSERT_FALSE(src.empty()) << "themer/main.c must be readable";

    // (1) The dangling return is gone: the body must not return the
    // destroyed dialog widget.
    const std::string body =
        fn_body(src, "create_filechooserdialog1(");
    ASSERT_FALSE(body.empty()) << "could not locate create_filechooserdialog1()";
    EXPECT_EQ(body.find("return dialog_startup"), std::string::npos) <<
        "create_filechooserdialog1() must not return the dialog "
        "widget it just destroyed (M24) — pre-fix main() showed the "
        "dangling pointer (g_critical on every -i run)";

    // (2) The signature is no longer a widget factory: the function
    // returns a success flag, so the caller has nothing to show.
    EXPECT_EQ(src.find("GtkWidget* create_filechooserdialog1"),
        std::string::npos) <<
        "create_filechooserdialog1() must not be declared to return "
        "a GtkWidget* (M24) — it destroys the dialog before returning; "
        "it should return a success flag";
    EXPECT_NE(src.find("gboolean create_filechooserdialog1"),
        std::string::npos) <<
        "create_filechooserdialog1() must return a success flag "
        "(ChatGPT follow-up: assert the positive contract, not only "
        "the absence of the widget return)";

    // (3) main() must not gtk_widget_show() the import dialog result.
    const std::string main_body = fn_body(src, "int main(");
    ASSERT_FALSE(main_body.empty()) << "could not locate main()";
    EXPECT_EQ(main_body.find("gtk_widget_show(filechooserdialog1)"),
        std::string::npos) <<
        "main() must not show the import dialog (M24) — it was "
        "destroyed inside create_filechooserdialog1()";
}
} // namespace
