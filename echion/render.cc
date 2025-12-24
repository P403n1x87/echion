#include <echion/frame.h>
#include <echion/render.h>

// ------------------------------------------------------------------------
void WhereRenderer::render_frame(Frame& frame)
{
    auto maybe_name_str = string_table.lookup(frame.name);
    if (!maybe_name_str)
    {
        std::cerr << "could not get name for render_frame" << std::endl;
        return;
    }
    const auto& name_str = maybe_name_str->get();

    auto maybe_filename_str = string_table.lookup(frame.filename);
    if (!maybe_filename_str)
    {
        std::cerr << "could not get filename for render_frame" << std::endl;
        return;
    }
    const auto& filename_str = maybe_filename_str->get();

    auto line = frame.location.line;
    auto col = frame.location.column;

    if (filename_str.rfind("native@", 0) == 0)
    {
        WhereRenderer::get().render_message(
            "\033[38;5;248;1m" + name_str + "\033[0m \033[38;5;246m(" + filename_str +
            "\033[0m:\033[38;5;246m" + std::to_string(line) + ":" + std::to_string(col) + ")\033[0m");
    }
    else
    {
        WhereRenderer::get().render_message("\033[33;1m" + name_str + "\033[0m (\033[36m" +
                                            filename_str + "\033[0m:\033[32m" +
                                            std::to_string(line) + "\033[0m)");
    }
}

// ------------------------------------------------------------------------
void MojoRenderer::render_frame(Frame& frame)
{
    const auto& file_name = string_table.lookup(frame.filename)->get();
    const auto& name = string_table.lookup(frame.name)->get();  

    // run $(which echion) python3 
    std::cerr
        << "Frame: " << " " << file_name 
        << " " << name 
        << " " << frame.location.line 
        << " " << frame.location.line_end 
        << " " << frame.location.column 
        << " " << frame.location.column_end 
        << " in_c_call=" << frame.in_c_call 
        << " cache_key=" << frame.cache_key
    << std::endl;
    frame_ref(frame.cache_key);
}
