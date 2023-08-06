# Taken from https://github.com/P403n1x87/dd-trace-py/blob/ac30ec213b9f6536106858ff67e6a0282f6cb393/ddtrace/internal/module.py

import sys
import typing as t
from collections import defaultdict
from importlib.abc import Loader
from importlib.machinery import ModuleSpec
from importlib.util import find_spec
from types import ModuleType


ModuleHookType = t.Callable[[ModuleType], None]
PreExecHookType = t.Callable[[t.Any, ModuleType], None]
PreExecHookCond = t.Union[str, t.Callable[[str], bool]]


# Borrowed from the wrapt module
# https://github.com/GrahamDumpleton/wrapt/blob/df0e62c2740143cceb6cafea4c306dae1c559ef8/src/wrapt/importer.py


def find_loader(fullname: str) -> t.Optional[Loader]:
    return getattr(find_spec(fullname), "loader", None)


class _ImportHookChainedLoader(Loader):
    def __init__(self, loader):
        # type: (Loader) -> None
        self.loader = loader
        self.callbacks: t.Dict[t.Any, t.Callable[[ModuleType], None]] = {}

        # DEV: load_module is deprecated so we define it at runtime if also
        # defined by the default loader. We also check and define for the
        # methods that are supposed to replace the load_module functionality.
        if hasattr(loader, "load_module"):
            self.load_module = self._load_module  # type: ignore[assignment]
        if hasattr(loader, "create_module"):
            self.create_module = self._create_module  # type: ignore[assignment]
        if hasattr(loader, "exec_module"):
            self.exec_module = self._exec_module  # type: ignore[assignment]

    def __getattribute__(self, name: str) -> t.Any:
        if name == "__class__":
            # Make isinstance believe that self is also an instance of
            # type(self.loader). This is required, e.g. by some tools, like
            # slotscheck, that can handle known loaders only.
            return self.loader.__class__

        return super(_ImportHookChainedLoader, self).__getattribute__(name)

    def __getattr__(self, name: str) -> t.Any:
        # Proxy any other attribute access to the underlying loader.
        return getattr(self.loader, name)

    def add_callback(
        self, key: t.Any, callback: t.Callable[[ModuleType], None]
    ) -> None:
        self.callbacks[key] = callback

    def _load_module(self, fullname: str) -> ModuleType:
        module = self.loader.load_module(fullname)
        for callback in self.callbacks.values():
            callback(module)

        return module

    def _create_module(self, spec) -> t.Optional[ModuleType]:
        return self.loader.create_module(spec)

    def _exec_module(self, module) -> None:
        # Collect and run only the first hook that matches the module.
        pre_exec_hook = None

        for _ in sys.meta_path:
            if isinstance(_, ModuleWatchdog):
                try:
                    for cond, hook in _._pre_exec_module_hooks:
                        if (
                            isinstance(cond, str)
                            and cond == module.__name__
                            or cond(module.__name__)
                        ):
                            # Several pre-exec hooks could match, we keep the first one
                            pre_exec_hook = hook
                            break
                except Exception:
                    pass

            if pre_exec_hook is not None:
                break

        if pre_exec_hook:
            pre_exec_hook(self, module)
        else:
            self.loader.exec_module(module)

        for callback in self.callbacks.values():
            callback(module)


class ModuleWatchdog(dict):
    """Module watchdog.

    Replace the standard ``sys.modules`` dictionary to detect when modules are
    loaded/unloaded. This is also responsible for triggering any registered
    import hooks.

    Subclasses might customize the default behavior by overriding the
    ``after_import`` method, which is triggered on every module import, once
    the subclass is installed.
    """

    _instance: t.Optional["ModuleWatchdog"] = None

    def __init__(self):
        # type: () -> None
        self._hook_map: t.DefaultDict[str, t.List[ModuleHookType]] = defaultdict(list)
        self._modules: t.Union[dict, ModuleWatchdog] = sys.modules
        self._finding: t.Set[str] = set()

    def __getitem__(self, item):
        # type: (str) -> ModuleType
        return self._modules.__getitem__(item)

    def __setitem__(self, name, module):
        # type: (str, ModuleType) -> None
        self._modules.__setitem__(name, module)

    def _add_to_meta_path(self):
        # type: () -> None
        sys.meta_path.insert(0, self)  # type: ignore[arg-type]

    @classmethod
    def _find_in_meta_path(cls) -> t.Optional[int]:
        for i, meta_path in enumerate(sys.meta_path):
            if type(meta_path) is cls:
                return i
        return None

    @classmethod
    def _remove_from_meta_path(cls):
        # type: () -> None
        i = cls._find_in_meta_path()
        if i is not None:
            sys.meta_path.pop(i)

    def after_import(self, module: ModuleType) -> None:
        # Collect all hooks
        hooks = []
        if module.__name__ in self._hook_map:
            hooks.extend(self._hook_map[module.__name__])

        if hooks:
            for hook in hooks:
                hook(module)

    def __delitem__(self, name: str) -> None:
        self._modules.__delitem__(name)

    def __getattribute__(self, name: str) -> t.Any:
        try:
            return super().__getattribute__("_modules").__getattribute__(name)
        except AttributeError:
            return super().__getattribute__(name)

    def __contains__(self, name: t.Any) -> bool:
        return self._modules.__contains__(name)

    def __len__(self) -> int:
        return self._modules.__len__()

    def __iter__(self) -> t.Iterator[str]:
        return self._modules.__iter__()

    def find_module(
        self, fullname: str, path: t.Optional[str] = None
    ) -> t.Optional[Loader]:
        if fullname in self._finding:
            return None

        self._finding.add(fullname)

        try:
            loader = find_loader(fullname)
            if loader is not None:
                if not isinstance(loader, _ImportHookChainedLoader):
                    loader = _ImportHookChainedLoader(loader)

                loader.add_callback(type(self), self.after_import)

                return loader

        finally:
            self._finding.remove(fullname)

        return None

    def find_spec(
        self,
        fullname: str,
        path: t.Optional[str] = None,
        target: t.Optional[ModuleType] = None,
    ) -> t.Optional[ModuleSpec]:
        if fullname in self._finding:
            return None

        self._finding.add(fullname)

        try:
            try:
                # Best effort
                spec = find_spec(fullname)
            except Exception:
                return None

            if spec is None:
                return None

            loader = getattr(spec, "loader", None)

            if loader is not None:
                if not isinstance(loader, _ImportHookChainedLoader):
                    spec.loader = _ImportHookChainedLoader(loader)

                t.cast(_ImportHookChainedLoader, spec.loader).add_callback(
                    type(self), self.after_import
                )

            return spec

        finally:
            self._finding.remove(fullname)

    @classmethod
    def register_module_hook(cls, module: str, hook: ModuleHookType) -> None:
        """Register a module hook.

        The hook will be called with the module object as argument.
        """
        cls._check_installed()

        instance = t.cast(ModuleWatchdog, cls._instance)
        instance._hook_map[module].append(hook)
        try:
            module_object = instance[module]
        except KeyError:
            # The module is not loaded yet. Nothing more we can do.
            return

        # The module was already imported so we invoke the hook straight-away
        hook(module_object)

    @classmethod
    def unregister_module_hook(cls, module: str, hook: ModuleHookType) -> None:
        """Unregister a module hook."""
        cls._check_installed()

        instance = t.cast(ModuleWatchdog, cls._instance)
        if module not in instance._hook_map:
            raise ValueError("No hooks registered for module %s" % module)

        try:
            if module in instance._hook_map:
                hooks = instance._hook_map[module]
                hooks.remove(hook)
                if not hooks:
                    del instance._hook_map[module]
        except ValueError:
            raise ValueError(
                "Hook %r not registered for module %r" % (hook, module)
            ) from None

    @classmethod
    def after_module_imported(cls, module: str) -> t.Callable[[ModuleHookType], None]:
        def _(hook: ModuleHookType) -> None:
            cls.register_module_hook(module, hook)

        return _

    @classmethod
    def _check_installed(cls) -> None:
        if not cls.is_installed():
            raise RuntimeError("%s is not installed" % cls.__name__)

    @classmethod
    def install(cls) -> None:
        """Install the module watchdog."""
        if cls.is_installed():
            raise RuntimeError("%s is already installed" % cls.__name__)

        cls._instance = sys.modules = cls()
        sys.modules._add_to_meta_path()

    @classmethod
    def is_installed(cls) -> bool:
        """Check whether this module watchdog class is installed."""
        return cls._instance is not None and type(cls._instance) is cls

    @classmethod
    def uninstall(cls) -> None:
        """Uninstall the module watchdog.

        This will uninstall only the most recently installed instance of this
        class.
        """
        cls._check_installed()

        parent, current = None, sys.modules
        while isinstance(current, ModuleWatchdog):
            if type(current) is cls:
                cls._remove_from_meta_path()
                if parent is not None:
                    parent._modules = current._modules
                else:
                    sys.modules = current._modules
                cls._instance = None
                return
            parent = current
            current = current._modules
