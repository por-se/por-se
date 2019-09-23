#pragma once

#include <optional>

#include "../TimingSolver.h"

#include "CommonTypes.h"

namespace klee {
  class WrappedTimingSolver : public SolverInterface {
    private:
      ExecutionState& state;
      TimingSolver& solver;
      time::Span timeout;

    public:
      WrappedTimingSolver(ExecutionState& st, TimingSolver& s, time::Span t) : state(st), solver(s), timeout(t) {};

      [[nodiscard]] std::optional<bool> mustBeTrue(ref<Expr> expr) const override {
        solver.setTimeout(timeout);
        bool result = true;
        bool success = solver.mustBeTrue(state, expr, result);
        solver.setTimeout(time::Span());

        if (!success) {
          return {};
        }

        return result;
      };

      [[nodiscard]] std::optional<bool> mustBeFalse(ref<Expr> expr) const override {
        solver.setTimeout(timeout);
        bool result = true;
        bool success = solver.mustBeFalse(state, expr, result);
        solver.setTimeout(time::Span());

        if (!success) {
          return {};
        }

        return result;
      };

      [[nodiscard]] std::optional<bool> mayBeTrue(ref<Expr> expr) const override {
        solver.setTimeout(timeout);
        bool result = true;
        bool success = solver.mayBeTrue(state, expr, result);
        solver.setTimeout(time::Span());

        if (!success) {
          return {};
        }

        return result;
      };

      [[nodiscard]] std::optional<bool> mayBeFalse(ref<Expr> expr) const override {
        solver.setTimeout(timeout);
        bool result = true;
        bool success = solver.mayBeFalse(state, expr, result);
        solver.setTimeout(time::Span());

        if (!success) {
          return {};
        }

        return result;
      };
  };
};