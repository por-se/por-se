#include "klee/Internal/Support/CallPrinter.h"

#include "llvm/IR/Argument.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"

#include <cstdint>

using namespace klee;

namespace {
  enum class FormattingType : std::uint8_t {
    UNKNOWN,
    SYMBOLIC,
    INTEGER,
    FLOAT,
    BOOLEAN,
    POINTER
  };

  static inline auto& fpWidthToSemantics(std::size_t width) {
    switch (width) {
      case Expr::Int32: return llvm::APFloat::IEEEsingle();
      case Expr::Int64: return llvm::APFloat::IEEEdouble();
      case Expr::Fl80: return llvm::APFloat::x87DoubleExtended();
      default: assert(0 && "Unsupported floating width");
    }
  }

  void printFunctionName(llvm::raw_ostream& os, const llvm::Function* f) {
    if (f->hasName()) {
      os << f->getName();
    } else {
      os << "<unnamed function>";
    }
  }

  void printValue(llvm::raw_ostream& os, const llvm::Type* typeInfo, ref<Expr> value) {
    FormattingType type = FormattingType::UNKNOWN;

    auto constValue = dyn_cast<ConstantExpr>(value.get());

    if (!constValue) {
      type = FormattingType::SYMBOLIC;
    } else if (typeInfo) {
      if (typeInfo->isPointerTy()) {
        type = FormattingType::POINTER;
      } else if (typeInfo->isFloatingPointTy()) {
        type = FormattingType::FLOAT;

        auto w = constValue->getWidth();
        if (w != Expr::Int32 && w != Expr::Int64 && w != Expr::Fl80) {
          type = FormattingType::UNKNOWN;
        }
      } else {
        // Since only two possible formats are left, guess the type based on the
        // bit width that llvm is using

        type = FormattingType::INTEGER;

        if (auto asIntType = dyn_cast<llvm::IntegerType>(typeInfo)) {
          type = asIntType->getBitWidth() == 1
            ? FormattingType::BOOLEAN : FormattingType::INTEGER;
        }
      }
    }

    switch (type) {
      case FormattingType::SYMBOLIC: {
        os << "<sym>";
        break;
      }
      case FormattingType::POINTER: {
        os << "0x" << constValue->getAPValue().toString(16, false);
        break;
      }
      case FormattingType::FLOAT: {
        auto apf = llvm::APFloat(fpWidthToSemantics(constValue->getWidth()), constValue->getAPValue());
        llvm::SmallVector<char, 16> tmpBuffer;
        apf.toString(tmpBuffer);
        os << tmpBuffer;
        break;
      }
      case FormattingType::BOOLEAN: {
        os << (constValue->getZExtValue() == 0 ? "false" : "true");
        break;
      }
      case FormattingType::UNKNOWN:
      case FormattingType::INTEGER: {
        os << constValue->getAPValue();
        break;
      }
    }
  }

  void printArgument(llvm::raw_ostream& os, const llvm::Argument* argType, ref<Expr> argValue) {
    if (argType && argType->hasName()) {
      os << argType->getName() << " = ";
    }

    auto* type = argType ? argType->getType() : nullptr;
    printValue(os, type, argValue);
  }
};

namespace klee {
  void CallPrinter::printCall(llvm::raw_ostream& os, llvm::Function* f, const std::vector<ref<Expr>>& args) {
    printFunctionName(os, f);

    bool first = true;

    os << '(';
    for (std::size_t i = 0; i < args.size(); i++) {
      if (first) {
        first = false;
      } else {
        os << ", ";
      }

      const auto* argType = i < f->arg_size() ? (f->args().begin() + i) : nullptr;
      auto argValue = args[i];

      printArgument(os, argType, argValue);
    }
    os << ')';
  }

  void CallPrinter::printCall(llvm::raw_ostream& os, KFunction* kf, const StackFrame& sf) {
    const auto* f = kf->function;
    printFunctionName(os, f);

    bool first = true;
    unsigned index = 0;

    os << '(';
    for (auto ai = f->arg_begin(), ae = f->arg_end(); ai != ae; ++ai) {
      if (first) {
        first = false;
      } else {
        os << ", ";
      }

      auto value = sf.locals[kf->getArgRegister(index)].value;
      printArgument(os, ai, value);

      index++;
    }

    if (sf.varargs && f->isVarArg()) {
      // So we can take varargs and there was a varargs argument passed to us
      // in theory we could reconstruct it with access to the state, but
      // this would require much work. Simply indicate here that there were
      // arguments
      os << ", ...";
    }

    os << ')';
  }

  void CallPrinter::printCallReturn(llvm::raw_ostream& os, llvm::Function* f, ref<Expr> value) {
    printFunctionName(os, f);

    auto* retType = f->getReturnType();

    os << " -> ";

    if (retType->isVoidTy()) {
      os << "<void>";
    } else if (value.get() != nullptr) {
      printValue(os, retType, value);
    } else {
      // KLEE does not always set a return value if one is technically needed ...
      os << "<undefined>";
    }
  }
};
