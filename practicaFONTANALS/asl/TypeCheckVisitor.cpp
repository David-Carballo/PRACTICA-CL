//////////////////////////////////////////////////////////////////////
//
//    TypeCheckVisitor - Walk the parser tree to do the semantic
//                       typecheck for the Asl programming language
//
//    Copyright (C) 2019  Universitat Politecnica de Catalunya
//
//    This library is free software; you can redistribute it and/or
//    modify it under the terms of the GNU General Public License
//    as published by the Free Software Foundation; either version 3
//    of the License, or (at your option) any later version.
//
//    This library is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//    Affero General Public License for more details.
//
//    You should have received a copy of the GNU Affero General Public
//    License along with this library; if not, write to the Free Software
//    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
//
//    contact: José Miguel Rivero (rivero@cs.upc.edu)
//             Computer Science Department
//             Universitat Politecnica de Catalunya
//             despatx Omega.110 - Campus Nord UPC
//             08034 Barcelona.  SPAIN
//
//////////////////////////////////////////////////////////////////////


#include "TypeCheckVisitor.h"

#include "antlr4-runtime.h"

#include "../common/TypesMgr.h"
#include "../common/SymTable.h"
#include "../common/TreeDecoration.h"
#include "../common/SemErrors.h"

#include <iostream>
#include <string>

// uncomment the following line to enable debugging messages with DEBUG*
// #define DEBUG_BUILD
#include "../common/debug.h"

// using namespace std;


// Constructor
TypeCheckVisitor::TypeCheckVisitor(TypesMgr       & Types,
				   SymTable       & Symbols,
				   TreeDecoration & Decorations,
				   SemErrors      & Errors) :
  Types{Types},
  Symbols {Symbols},
  Decorations{Decorations},
  Errors{Errors} {
}

// Methods to visit each kind of node:
//
antlrcpp::Any TypeCheckVisitor::visitProgram(AslParser::ProgramContext *ctx) {
  // You don't want to know what happens when you read this without having
  // written to it.
  Symbols.setCurrentFunctionTy(Types.createErrorTy());
  SymTable::ScopeId sc = getScopeDecor(ctx);
  Symbols.pushThisScope(sc);  
  for (auto ctxFunc : ctx->function()) { 
    visit(ctxFunc);
  }
  if (Symbols.noMainProperlyDeclared())
    Errors.noMainProperlyDeclared(ctx);
  Symbols.popScope();
  Errors.print();
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitFunction(AslParser::FunctionContext *ctx) {
  // LOL! SymTable is so stupid that we have to set the last function's type ourselves,
  // instead of taking advantage of the last ScopeId. Thanks for nothing!
  // And we have to do this before diving into the function's scope because a local variable
  // might override the function's name. Otherwise we'd have to add a type decorator
  // to the function node itself.
  // EDIT: Okay, using decorators is unavoidable. We cannot rely on the scope as there
  // may be two functions with the same name. Even if the program doesn't compile,
  // the warnings won't not appropiate.
  Symbols.setCurrentFunctionTy(getTypeDecor(ctx));
  SymTable::ScopeId sc = getScopeDecor(ctx);
  Symbols.pushThisScope(sc);
  visit(ctx->statements());
  Symbols.popScope();
  Symbols.setCurrentFunctionTy(Types.createErrorTy());
  return 0;
}

// antlrcpp::Any TypeCheckVisitor::visitDeclarations(AslParser::DeclarationsContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

// antlrcpp::Any TypeCheckVisitor::visitVariable_decl(AslParser::Variable_declContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

// antlrcpp::Any TypeCheckVisitor::visitType(AslParser::TypeContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

antlrcpp::Any TypeCheckVisitor::visitStatements(AslParser::StatementsContext *ctx) {
  visitChildren(ctx);
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitAssignStmt(AslParser::AssignStmtContext *ctx) {
  auto &&left = ctx->left_expr();
  visit(left);
  auto &&right = ctx->expr();
  visit(right);
  auto &&t1 = getTypeDecor(left);
  auto &&t2 = getTypeDecor(right);
  if (!Types.isErrorTy(t1) && !Types.isErrorTy(t2) &&
      !Types.isVoidTy(t2) && !Types.copyableTypes(t1, t2))
    Errors.incompatibleAssignment(ctx->ASSIGN());
  if (!Types.isErrorTy(t1) && !getIsLValueDecor(left))
    Errors.nonReferenceableLeftExpr(left);
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitIfStmt(AslParser::IfStmtContext *ctx) {
  visit(ctx->expr());
  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr());
  if ((not Types.isErrorTy(t1)) and (not Types.isBooleanTy(t1)))
    Errors.booleanRequired(ctx);
  const auto &&statements = ctx->statements();
  visit(statements[0]);
  if (statements.size() > 1)
    visit(statements[1]);
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitWhileLoop(AslParser::WhileLoopContext *ctx) {
  visit(ctx->expr());
  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr());
  if ((not Types.isErrorTy(t1)) and (not Types.isBooleanTy(t1)))
    Errors.booleanRequired(ctx);
  visit(ctx->statements());
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitInvocation(AslParser::InvocationContext *ctx) {
  auto &&ident = ctx->ident();
  visit(ident);
  auto &&type = getTypeDecor(ident);
  if (!Types.isErrorTy(type)) {
    if (Types.isFunctionTy(type)) {
      putTypeDecor(ctx, Types.getFuncReturnType(type));
      auto &&arguments = ctx->expr();
      auto &&parameterTypes = Types.getFuncParamsTypes(type);
      if (arguments.size() != parameterTypes.size())
        Errors.numberOfParameters(ident);
      size_t i = 0;
      for (auto &&argument : arguments) {
        // Always visit arguments, as they may contain errors.
        visit(argument);
        if (i < parameterTypes.size() && !Types.copyableTypes(parameterTypes[i], getTypeDecor(argument)))
          Errors.incompatibleParameter(argument, i+1, ctx); // Error takes argument numbers starting at 1
        ++i;
      }
    } else
      Errors.isNotCallable(ident);
  }
  putIsLValueDecor(ctx, false);
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitReadStmt(AslParser::ReadStmtContext *ctx) {
  visit(ctx->left_expr());
  TypesMgr::TypeId t1 = getTypeDecor(ctx->left_expr());
  if ((not Types.isErrorTy(t1)) and (not Types.isPrimitiveTy(t1)) and
      (not Types.isFunctionTy(t1)))
    Errors.readWriteRequireBasic(ctx);
  if ((not Types.isErrorTy(t1)) and (not getIsLValueDecor(ctx->left_expr())))
    Errors.nonReferenceableExpression(ctx);
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitWriteExpr(AslParser::WriteExprContext *ctx) {
  visit(ctx->expr());
  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr());
  if ((not Types.isErrorTy(t1)) and (not Types.isPrimitiveTy(t1)))
    Errors.readWriteRequireBasic(ctx);
  return 0;
}

// antlrcpp::Any TypeCheckVisitor::visitWriteString(AslParser::WriteStringContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

antlrcpp::Any TypeCheckVisitor::visitRetStmt(AslParser::RetStmtContext *ctx) {
  auto &&rt = Symbols.getCurrentFunctionTy();
  auto &&expr = ctx->expr();
  if (!Types.isErrorTy(rt)) {
    rt = Types.getFuncReturnType(rt);
    bool err;
    if (expr) {
      visit(expr); // Always visit the expression.
      auto &&et = getTypeDecor(expr);
      err = !Types.isErrorTy(et) && !Types.copyableTypes(rt, et);
    } else
      err = !Types.isVoidTy(rt);
    if (err)
      Errors.incompatibleReturn(ctx->RETURN());
  }
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitLeft_expr(AslParser::Left_exprContext *ctx) {
  antlr4::ParserRuleContext *left;
  if ((left = ctx->ident())) {}
  else if ((left = ctx->subscript())) {}
  visit(left);
  putTypeDecor(ctx, getTypeDecor(left));
  putIsLValueDecor(ctx, getIsLValueDecor(left));
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitArithmetic(AslParser::ArithmeticContext *ctx) {
  visit(ctx->expr(0));
  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr(0));
  visit(ctx->expr(1));
  TypesMgr::TypeId t2 = getTypeDecor(ctx->expr(1));
  // Pointer to member function just for the lols.
  auto &&checkNumeric = ctx->MOD() ? &TypesMgr::isIntegerTy : &TypesMgr::isNumericTy;
  if (((not Types.isErrorTy(t1)) and (not (Types.*checkNumeric)(t1))) or
      ((not Types.isErrorTy(t2)) and (not (Types.*checkNumeric)(t2))))
    Errors.incompatibleOperator(ctx->op);
  // Return type: Integers do implicit cast-to-Float
  auto &&t =
    (Types.isFloatTy(t1) || Types.isFloatTy(t2)) ? Types.createFloatTy()  :
                                                   Types.createIntegerTy();
  putTypeDecor(ctx, t);
  putIsLValueDecor(ctx, false);
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitArithmeticUnary(AslParser::ArithmeticUnaryContext *ctx) {
  visit(ctx->expr());
  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr());
  if ((not Types.isErrorTy(t1)) and (not Types.isNumericTy(t1)))
    Errors.incompatibleOperator(ctx->op);
  TypesMgr::TypeId t = Types.createIntegerTy();
  putTypeDecor(ctx, t);
  putIsLValueDecor(ctx, false);
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitBoolean(AslParser::BooleanContext *ctx) {
  visit(ctx->expr(0));
  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr(0));
  visit(ctx->expr(1));
  TypesMgr::TypeId t2 = getTypeDecor(ctx->expr(1));
  if (((not Types.isErrorTy(t1)) and (not Types.isBooleanTy(t1))) or
      ((not Types.isErrorTy(t2)) and (not Types.isBooleanTy(t2))))
    Errors.incompatibleOperator(ctx->op);
  TypesMgr::TypeId t = Types.createBooleanTy();
  putTypeDecor(ctx, t);
  putIsLValueDecor(ctx, false);
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitBooleanUnary(AslParser::BooleanUnaryContext *ctx) {
  visit(ctx->expr());
  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr());
  if ((not Types.isErrorTy(t1)) and (not Types.isBooleanTy(t1)))
    Errors.incompatibleOperator(ctx->op);
  TypesMgr::TypeId t = Types.createBooleanTy();
  putTypeDecor(ctx, t);
  putIsLValueDecor(ctx, false);
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitRelational(AslParser::RelationalContext *ctx) {
  visit(ctx->expr(0));
  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr(0));
  visit(ctx->expr(1));
  TypesMgr::TypeId t2 = getTypeDecor(ctx->expr(1));
  std::string oper = ctx->op->getText();
  if ((not Types.isErrorTy(t1)) and (not Types.isErrorTy(t2)) and
      (not Types.comparableTypes(t1, t2, oper)))
    Errors.incompatibleOperator(ctx->op);
  TypesMgr::TypeId t = Types.createBooleanTy();
  putTypeDecor(ctx, t);
  putIsLValueDecor(ctx, false);
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitSubscript(AslParser::SubscriptContext *ctx) {
  {
    auto &&ident = ctx->ident();
    visit(ident);
    auto &&t = getTypeDecor(ident);
    if (Types.isArrayTy(t))
      putTypeDecor(ctx, Types.getArrayElemType(t));
    else {
      if (!Types.isErrorTy(t))
        Errors.nonArrayInArrayAccess(ctx);
      putTypeDecor(ctx, Types.createErrorTy());
    }
  }
  {
    auto &&expr = ctx->expr();
    visit(expr);
    auto &&t = getTypeDecor(expr);
    if (!Types.isErrorTy(t) && !Types.isIntegerTy(t))
      Errors.nonIntegerIndexInArrayAccess(expr);
  }
  putIsLValueDecor(ctx, true);
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitExprSubscript(AslParser::ExprSubscriptContext *ctx) {
  copyDecor(ctx, ctx->subscript());
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitExprCall(AslParser::ExprCallContext *ctx) {
  auto &&invocation = ctx->invocation();
  copyDecor(ctx, invocation);
  auto &&t = getTypeDecor(ctx);
  if (!Types.isErrorTy(t) && Types.isVoidTy(t))
    // Expressions are the only place where functions returning void
    // cannot be invoked, this function is a good place to make this check.
    Errors.isNotFunction(invocation->ident());
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitValue(AslParser::ValueContext *ctx) {
  if (ctx->INTVAL())
    putTypeDecor(ctx, Types.createIntegerTy());
  else if (ctx->FLOATVAL())
    putTypeDecor(ctx, Types.createFloatTy());
  else if (ctx->CHARVAL())
    putTypeDecor(ctx, Types.createCharacterTy());
  else if (ctx->BOOLVAL())
    putTypeDecor(ctx, Types.createBooleanTy());
  putIsLValueDecor(ctx, false);
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitExprIdent(AslParser::ExprIdentContext *ctx) {
  copyDecor(ctx, ctx->ident());
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitIdent(AslParser::IdentContext *ctx) {
  std::string ident = ctx->getText();
  if (Symbols.findInStack(ident) == -1) {
    Errors.undeclaredIdent(ctx->ID());
    putTypeDecor(ctx, Types.createErrorTy());
    putIsLValueDecor(ctx, true);
  } else {
    putTypeDecor(ctx, Symbols.getType(ident));
    if (Symbols.isFunctionClass(ident))
      putIsLValueDecor(ctx, false);
    else
      putIsLValueDecor(ctx, true);
  }
  return 0;
}

void TypeCheckVisitor::copyDecor(antlr4::ParserRuleContext *dst, antlr4::ParserRuleContext *src) {
  visit(src);
  putTypeDecor(dst, getTypeDecor(src));
  putIsLValueDecor(dst, getIsLValueDecor(src));
}

// Getters for the necessary tree node atributes:
//   Scope, Type ans IsLValue
SymTable::ScopeId TypeCheckVisitor::getScopeDecor(antlr4::ParserRuleContext *ctx) {
  return Decorations.getScope(ctx);
}
TypesMgr::TypeId TypeCheckVisitor::getTypeDecor(antlr4::ParserRuleContext *ctx) {
  return Decorations.getType(ctx);
}
bool TypeCheckVisitor::getIsLValueDecor(antlr4::ParserRuleContext *ctx) {
  return Decorations.getIsLValue(ctx);
}

// Setters for the necessary tree node attributes:
//   Scope, Type ans IsLValue
void TypeCheckVisitor::putScopeDecor(antlr4::ParserRuleContext *ctx, SymTable::ScopeId s) {
  Decorations.putScope(ctx, s);
}
void TypeCheckVisitor::putTypeDecor(antlr4::ParserRuleContext *ctx, TypesMgr::TypeId t) {
  Decorations.putType(ctx, t);
}
void TypeCheckVisitor::putIsLValueDecor(antlr4::ParserRuleContext *ctx, bool b) {
  Decorations.putIsLValue(ctx, b);
}
