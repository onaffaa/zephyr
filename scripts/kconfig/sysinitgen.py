# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: Apache-2.0

"""
Assigns SYS_INIT priorities programmatically to Kconfig symbols with the
suffix "SYS_INIT_GEN", ordered according to their "depends on" property.

Used by the 'kconfig' build target (scripts/kconfig/kconfig.py).
"""

import sys

from kconfiglib import AND, INT, NOT, OR, STR_TO_TRI, MenuNode, Symbol, expr_value

# Marker suffix for Kconfig symbols whose SYS_INIT priority is assigned
# programmatically, based on their "depends on".
SYS_INIT_MARKER = "SYS_INIT_GEN"


class SysInitSym:
    """
    One SYS_INIT_GEN symbol: tracks its dependencies/dependents among other
    SYS_INIT_GEN symbols and the synthesized "_PRIO" int symbol that carries
    its resolved SYS_INIT priority.
    """

    def __init__(self, sym, container):
        self.prio = container.prio_low
        self.orig_dependents = sym._dependents
        self.orig_dependencies = []
        self.find_sys_init_deps(sym.direct_dep)

        self.resolved = len(self.orig_dependencies) == 0

        # we are setting the value of the synthesized prio symbol,
        # so we need to also synthesize a prompt
        prompt_node = MenuNode()
        prompt_node.item = None
        prompt_node.prompt = ("Synthesized", container.kconf.y)

        # generate prio symbol
        priosym = Symbol()
        priosym.kconfig = container.kconf
        priosym.name = f"{sym.name}_PRIO"
        priosym.is_constant = False
        priosym.rev_dep = priosym.weak_rev_dep = priosym.direct_dep = container.kconf.n
        priosym.orig_type = INT
        priosym.nodes = [prompt_node]
        priosym.user_value = None
        priosym.set_value(str(self.prio))

        self.user_sym = sym
        self.prio_sym = priosym
        self.dependencies = []
        self.dependents = []

    def find_sys_init_deps(self, expr):
        """
        Find the set of sys-init dependencies for this symbol by evaluating
        the depends-on expression and saving each element that is a
        SYS_INIT_GEN symbol.
        This code is copied directly from kconfiglib.eval_expr with the
        addition of the orig_dependency saving.
        """
        if expr.__class__ is not tuple:
            # If we hit a final symbol, and it is a SYS_INIT_GEN one, then save
            # it as a dependency that needs to be used for priority calculation.
            if expr.name.endswith(SYS_INIT_MARKER):
                self.orig_dependencies.append(expr)
            return expr.tri_value

        if expr[0] is AND:
            v1 = self.find_sys_init_deps(expr[1])
            # Short-circuit the n case as an optimization (~5% faster
            # allnoconfig.py and allyesconfig.py, as of writing)
            return 0 if not v1 else min(v1, self.find_sys_init_deps(expr[2]))

        if expr[0] is OR:
            v1 = self.find_sys_init_deps(expr[1])
            # Short-circuit the y case as an optimization
            return (
                STR_TO_TRI["y"]
                if v1 == STR_TO_TRI["y"]
                else max(v1, self.find_sys_init_deps(expr[2]))
            )

        if expr[0] is NOT:
            return STR_TO_TRI["y"] - self.find_sys_init_deps(expr[1])

    def adjust_prio(self):
        if not self.resolved:
            # The symbols priority not known to be above all its dependencies
            new_prio = max([dep_sym.prio for dep_sym in self.dependencies]) + 1

            if new_prio != self.prio:
                self.prio = new_prio
                self.resolved = True

                for dependent in self.dependents:
                    if dependent.prio <= self.prio:
                        dependent.resolved = False
                return True

        return False


class SysInitSet:
    """
    Helper class to coordinate all symbols which factor in to dependency prio
    """

    DEFAULT_PRIO_LOW = 41
    DEFAULT_PRIO_HIGH = 59

    def __init__(self, kconf, level, warn, err):
        self.prio_low = self.DEFAULT_PRIO_LOW
        self.prio_high = self.DEFAULT_PRIO_HIGH

        self.level = level
        self.kconf = kconf
        self.warn = warn
        self.err = err

        # Assign a low and high priority for a given SYS_INIT
        # level based on the value defined in subsys/sys_init_gen/Kconfig
        for sym in self.kconf.unique_defined_syms:
            if sym.name == f"{SYS_INIT_MARKER}_{level}_PRE":
                try:
                    self.prio_low = int(sym.str_value) + 1
                except (ValueError, TypeError):
                    self.err(f"{sym.name} must be of type INT")
            elif sym.name == f"{SYS_INIT_MARKER}_{level}_POST":
                try:
                    self.prio_high = int(sym.str_value) - 1
                except (ValueError, TypeError):
                    self.err(f"{sym.name} must be of type INT")

        self.sym_dict = {}

        self.collect_syms()
        self.build_dependency_graph()

    def collect_syms(self):
        # Add all Kconfig symbols that end with "SYS_INIT_MARKER" and evaluate to "y"
        for sym in self.kconf.unique_defined_syms:
            if not sym.name.endswith(SYS_INIT_MARKER):
                continue

            if sym.str_value == "y":
                self.add_sym(sym)
            else:
                # Warn if a symbols value and condition both evaluate to "y" but
                # the symbol itself did not evaluate to "y"
                for default in sym.orig_defaults:
                    if all([expr_value(def_expr) == STR_TO_TRI["y"] for def_expr in default]):
                        self.warn(
                            f"{sym.name} has unmet dependencies: "
                            f"{[x.name for x in sym.referenced if x.str_value != 'y']}"
                        )

    def build_dependency_graph(self):
        for name, sym in self.sym_dict.items():
            # Link the symbol to the tracked symbols containing "SYS_INIT_MARKER" that
            # depend on it, so that raising symbols priority can mark them unresolved again.
            for dependent in sym.orig_dependents:
                if (
                    dependent.name in self.sym_dict
                    and self.sym_dict[dependent.name] not in sym.dependents
                ):
                    sym.dependents.append(self.sym_dict[dependent.name])

            # Link the symbol to the tracked symbols containing "SYS_INIT_MARKER" that it
            # depends on, so that its priority can be computed from theirs.
            for dependency in sym.orig_dependencies:
                if dependency.name in self.sym_dict:
                    if self.sym_dict[dependency.name] not in sym.dependencies:
                        sym.dependencies.append(self.sym_dict[dependency.name])
                else:
                    self.warn(f"{name} depends on {dependency.name} which is undefined")

    def add_sym(self, sym):
        self.sym_dict[sym.name] = SysInitSym(sym, self)
        self.kconf.unique_defined_syms.append(self.sym_dict[sym.name].prio_sym)

    def resolve_prios(self):
        while not self.resolved():
            for sym in self.sym_dict.values():
                if len(sym.dependencies) == 0:
                    sym.prio = self.prio_low
                    sym.resolved = True
                else:
                    if sym.adjust_prio():
                        self.coalesce()
                    if self.resolved():
                        break
                    if sym.prio >= self.prio_high:
                        self.err("Aborting due to kconfig SYS_INIT dependency cycle")
        self.set_prios()

    def set_prios(self):
        # Write each symbol's resolved priority into its synthesized prio symbol
        for sym in self.sym_dict.values():
            sym.prio_sym.set_value(str(sym.prio))

    def coalesce(self):
        # Re-check whether each symbol's priority is still higher than all of
        # its dependencies', now that some dependency's priority has changed
        for sym in self.sym_dict.values():
            if len(sym.dependencies) > 0:
                sym.resolved = max([dep_sym.prio for dep_sym in sym.dependencies]) < sym.prio

    def resolved(self):
        # True once every tracked symbol has a final, stable priority
        return all([sym.resolved for sym in self.sym_dict.values()])

    def print_report(self):
        if len(self.sym_dict) == 0:
            return

        report = f"SYS_INIT Generated Priorities @ {self.level}\n"
        report += "Prio\tSymbol [Dependencies]\n"

        for sym in sorted(self.sym_dict.values(), key=lambda x: x.prio):
            report += (
                f"{sym.prio:<4}\t{sym.user_sym.name} "
                f"{[s.user_sym.name for s in sym.dependencies]}\n"
            )

        print(report, file=sys.stdout)
