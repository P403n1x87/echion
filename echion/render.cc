#include <echion/frame.h>
#include <echion/render.h>

// ------------------------------------------------------------------------
[[nodiscard]] Result<void> WhereRenderer::render_frame_internal(Frame& frame)
{
    auto maybe_name = string_table.lookup(frame.name);
    if (!maybe_name)
        return Result<void>::error(ErrorKind::LookupError);

    auto maybe_filename = string_table.lookup(frame.filename);
    if (!maybe_filename)
        return Result<void>::error(ErrorKind::LookupError);

    const std::string& name = **maybe_name;
    const std::string& filename = **maybe_filename;
    auto line = frame.location.line;

    if (filename.rfind("native@", 0) == 0)
    {
        WhereRenderer::get().render_message(
            "\033[38;5;248;1m" + name + "\033[0m \033[38;5;246m(" + filename +
            "\033[0m:\033[38;5;246m" + std::to_string(line) + ")\033[0m");
    }
    else
    {
        WhereRenderer::get().render_message("\033[33;1m" + name + "\033[0m (\033[36m" +
                                            filename + "\033[0m:\033[32m" +
                                            std::to_string(line) + "\033[0m)");
    }

    return Result<void>::ok();
}

void WhereRenderer::render_frame(Frame& frame)
{
    // result is intentionally ignored
    (void)render_frame_internal(frame);
}

// ------------------------------------------------------------------------
void MojoRenderer::render_frame(Frame& frame)
{
    frame_ref(frame.cache_key);
}
