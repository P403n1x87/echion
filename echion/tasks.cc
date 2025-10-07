#include <echion/tasks.h>

#include <stack>

const char* GenInfo::Error::what() const noexcept
{
    return "Cannot create generator info object";
}

GenInfo::GenInfo(PyObject* gen_addr)
{
    PyGenObject gen;

    if (copy_type(gen_addr, gen) || !PyCoro_CheckExact(&gen))
        throw Error();

    origin = gen_addr;

#if PY_VERSION_HEX >= 0x030b0000
    // The frame follows the generator object
    frame = (gen.gi_frame_state == FRAME_CLEARED)
                ? NULL
                : (PyObject*)((char*)gen_addr + offsetof(PyGenObject, gi_iframe));
#else
    frame = (PyObject*)gen.gi_frame;
#endif

    PyFrameObject f;
    if (copy_type(frame, f))
        throw Error();

    PyObject* yf = (frame != NULL ? PyGen_yf(&gen, frame) : NULL);
    if (yf != NULL && yf != gen_addr)
    {
        try
        {
            await = std::make_unique<GenInfo>(yf);
        }
        catch (GenInfo::Error&)
        {
            await = nullptr;
        }
    }

#if PY_VERSION_HEX >= 0x030b0000
    is_running = (gen.gi_frame_state == FRAME_EXECUTING);
#elif PY_VERSION_HEX >= 0x030a0000
    is_running = (frame != NULL) ? _PyFrame_IsExecuting(&f) : false;
#else
    is_running = gen.gi_running;
#endif
}
const char* TaskInfo::Error::what() const noexcept
{
    return "Cannot create task info object";
}
const char* TaskInfo::GeneratorError::what() const noexcept
{
    return "Cannot create generator info object";
}

// ----------------------------------------------------------------------------
TaskInfo::TaskInfo(TaskObj* task_addr)
{
    TaskObj task;
    if (copy_type(task_addr, task))
        throw Error();

    try
    {
        coro = std::make_unique<GenInfo>(task.task_coro);
    }
    catch (GenInfo::Error&)
    {
        throw GeneratorError();
    }

    origin = (PyObject*)task_addr;

    try
    {
        name = string_table.key(task.task_name);
    }
    catch (StringTable::Error&)
    {
        throw Error();
    }

    loop = task.task_loop;

    if (task.task_fut_waiter)
    {
        try
        {
            waiter =
                std::make_unique<TaskInfo>((TaskObj*)task.task_fut_waiter);  // TODO: Make lazy?
        }
        catch (TaskInfo::Error&)
        {
            waiter = nullptr;
        }
    }
}

// ----------------------------------------------------------------------------
TaskInfo TaskInfo::current(PyObject* loop)
{
    if (loop == NULL)
        throw Error();

    try
    {
        MirrorDict current_tasks_dict(asyncio_current_tasks);
        PyObject* task = current_tasks_dict.get_item(loop);
        if (task == NULL)
            throw Error();

        return TaskInfo((TaskObj*)task);
    }
    catch (MirrorError& e)
    {
        throw Error();
    }
}

// ----------------------------------------------------------------------------
// TODO: Make this a "for_each_task" function?
std::vector<TaskInfo::Ptr> get_all_tasks(PyObject* loop)
{
    std::vector<TaskInfo::Ptr> tasks;
    if (loop == NULL)
        return tasks;

    try
    {
        MirrorSet scheduled_tasks_set(asyncio_scheduled_tasks);
        auto scheduled_tasks = scheduled_tasks_set.as_unordered_set();

        for (auto task_wr_addr : scheduled_tasks)
        {
            PyWeakReference task_wr;
            if (copy_type(task_wr_addr, task_wr))
                continue;

            try
            {
                auto task_info = std::make_unique<TaskInfo>((TaskObj*)task_wr.wr_object);
                if (task_info->loop == loop)
                    tasks.push_back(std::move(task_info));
            }
            catch (TaskInfo::Error& e)
            {
                // We failed to get this task but we keep going
            }
        }

        if (asyncio_eager_tasks != NULL)
        {
            MirrorSet eager_tasks_set(asyncio_eager_tasks);
            auto eager_tasks = eager_tasks_set.as_unordered_set();

            for (auto task_addr : eager_tasks)
            {
                try
                {
                    auto task_info = std::make_unique<TaskInfo>((TaskObj*)task_addr);
                    if (task_info->loop == loop)
                        tasks.push_back(std::move(task_info));
                }
                catch (TaskInfo::Error& e)
                {
                    // We failed to get this task but we keep going
                }
            }
        }

        return tasks;
    }
    catch (MirrorError& e)
    {
        throw TaskInfo::Error();
    }
}

// ----------------------------------------------------------------------------
size_t TaskInfo::unwind(FrameStack& stack)
{
    // TODO: Check for running task.
    std::stack<PyObject*> coro_frames;

    // Unwind the coro chain
    for (auto coro = this->coro.get(); coro != NULL; coro = coro->await.get())
    {
        if (coro->frame != NULL)
            coro_frames.push(coro->frame);
    }

    int count = 0;

    // Unwind the coro frames
    while (!coro_frames.empty())
    {
        PyObject* frame = coro_frames.top();
        coro_frames.pop();

        count += unwind_frame(frame, stack);
    }

    return count;
}
