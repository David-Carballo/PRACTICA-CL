//////////////////////////////////////////////////////////////////////
//
//    SymbolsVisitor - Walk the parser tree to register symbols
//                     for the Asl programming language
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

#include "SymbolsVisitor.h"

#include "antlr4-runtime.h"

#include "../common/TypesMgr.h"
#include "../common/SymTable.h"
#include "../common/TreeDecoration.h"
#include "../common/SemErrors.h"

#include <iostream>
#include <string>
#include <vector>

#include <cstddef>    // std::size_t

// uncomment the following line to enable debugging messages with DEBUG*
// #define DEBUG_BUILD
#include "../common/debug.h"

// using namespace std;


// Constructor
SymbolsVisitor::SymbolsVisitor(TypesMgr       & Types,
			       SymTable       & Symbols,
			       TreeDecoration & Decorations,
			       SemErrors      & Errors) :
  Types{Types},
  Symbols{Symbols},
  Decorations{Decorations},
  Errors{Errors} {
}


// Methods to visit each kind of node:
//
antlrcpp::Any SymbolsVisitor::visitProgram(AslParser::ProgramContext *ctx) {
  DEBUG_ENTER();
  auto &&sc = Symbols.pushNewScope("$global$");
  putScopeDecor(ctx, sc);
  for (auto ctxFunc : ctx->function())
    visit(ctxFunc);
//   Symbols.print();
  Symbols.popScope();
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any SymbolsVisitor::visitFunction(AslParser::FunctionContext *ctx) {
  DEBUG_ENTER();
  auto &&funcName = ctx->ID()->getText();
  auto &&ident = ctx->ID()->getText();
  if (Symbols.findInCurrentScope(ident))
    Errors.declaredIdent(ctx->ID());
  else {
    auto &&sc = Symbols.pushNewScope(funcName);
    putScopeDecor(ctx, sc);
    std::vector<TypesMgr::TypeId> lParamsTy;
    {
      auto &&parameters = ctx->parameters();
      visit(parameters);
      auto &&parameter_decl = parameters->parameter_decl();
      lParamsTy.reserve(parameter_decl.size());
      for (auto &&parameter : parameter_decl)
        lParamsTy.push_back(getTypeDecor(parameter));
    }
    TypesMgr::TypeId tRet;
    {
      auto &&type = ctx->type();
      if (type) {
        visit(type);
        tRet = getTypeDecor(type);
      } else
        tRet = Types.createVoidTy();
    }
    visit(ctx->declarations());
    visit(ctx->statements());
//     Symbols.print();
    Symbols.popScope();
    auto &&tFunc = Types.createFunctionTy(lParamsTy, tRet);
    Symbols.addFunction(ident, tFunc);
    putTypeDecor(ctx, tFunc);
  }
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any SymbolsVisitor::visitParameters(AslParser::ParametersContext *ctx) {
  DEBUG_ENTER();
  visitChildren(ctx);
  DEBUG_EXIT();
  return 0;
}


antlrcpp::Any SymbolsVisitor::visitDeclarations(AslParser::DeclarationsContext *ctx) {
  DEBUG_ENTER();
  visitChildren(ctx);
  DEBUG_EXIT();
  return 0;
}

// parameter_decl and variable_decl are similar except for the fact that variable_decl
// has an array of ID.

void SymbolsVisitor::addVariable(antlr4::tree::TerminalNode* ID, TypesMgr::TypeId t) {
  auto &&ident = ID->getText();
  if (Symbols.findInCurrentScope(ident))
    Errors.declaredIdent(ID);
  else
    Symbols.addLocalVar(ident, t);
}

antlrcpp::Any SymbolsVisitor::visitParameter_decl(AslParser::Parameter_declContext *ctx) {
  DEBUG_ENTER();
  auto &&type = ctx->type();
  visit(type);
  auto &&t = getTypeDecor(type);
  putTypeDecor(ctx, t);
  addVariable(ctx->ID(), t);
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any SymbolsVisitor::visitVariable_decl(AslParser::Variable_declContext *ctx) {
  DEBUG_ENTER();
  auto &&type = ctx->type();
  visit(type);
  auto &&t = getTypeDecor(type);
  putTypeDecor(ctx, t);
  for (auto &&ID : ctx->ID())
    addVariable(ID, t);
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any SymbolsVisitor::visitTypePrimitive(AslParser::TypePrimitiveContext *ctx) {
  DEBUG_ENTER();
  auto &&primitive = ctx->primitive();
  visit(primitive);
  putTypeDecor(ctx, getTypeDecor(primitive));
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any SymbolsVisitor::visitTypeArray(AslParser::TypeArrayContext *ctx) {
  DEBUG_ENTER();
  auto &&primitive = ctx->primitive();
  visit(primitive);
  auto &&t = getTypeDecor(primitive); // FIXME: Array of void not allowed.
  auto &&size = std::stoi(ctx->INTVAL()->getText());
  putTypeDecor(ctx, Types.createArrayTy(size, t));
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any SymbolsVisitor::visitPrimitive(AslParser::PrimitiveContext *ctx) {
  DEBUG_ENTER();
  if (ctx->INT())
    putTypeDecor(ctx, Types.createIntegerTy());
  else if (ctx->FLOAT())
    putTypeDecor(ctx, Types.createFloatTy());
  else if (ctx->BOOL())
    putTypeDecor(ctx, Types.createBooleanTy());
  else if (ctx->CHAR())
    putTypeDecor(ctx, Types.createCharacterTy());
  else if (ctx->VOID())
    putTypeDecor(ctx, Types.createVoidTy());
  DEBUG_EXIT();
  return 0;
}

// antlrcpp::Any SymbolsVisitor::visitStatements(AslParser::StatementsContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

// antlrcpp::Any SymbolsVisitor::visitAssignStmt(AslParser::AssignStmtContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

// antlrcpp::Any SymbolsVisitor::visitIfStmt(AslParser::IfStmtContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

// antlrcpp::Any SymbolsVisitor::visitProcCall(AslParser::ProcCallContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

// antlrcpp::Any SymbolsVisitor::visitReadStmt(AslParser::ReadStmtContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

// antlrcpp::Any SymbolsVisitor::visitWriteExpr(AslParser::WriteExprContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

// antlrcpp::Any SymbolsVisitor::visitWriteString(AslParser::WriteStringContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

// antlrcpp::Any SymbolsVisitor::visitLeft_expr(AslParser::Left_exprContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

// antlrcpp::Any SymbolsVisitor::visitExprIdent(AslParser::ExprIdentContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

// antlrcpp::Any SymbolsVisitor::visitArithmetic(AslParser::ArithmeticContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

// antlrcpp::Any SymbolsVisitor::visitRelational(AslParser::RelationalContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

// antlrcpp::Any SymbolsVisitor::visitValue(AslParser::ValueContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

// antlrcpp::Any SymbolsVisitor::visitIdent(AslParser::IdentContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

// Getters for the necessary tree node atributes:
//   Scope and Type
SymTable::ScopeId SymbolsVisitor::getScopeDecor(antlr4::ParserRuleContext *ctx) {
  return Decorations.getScope(ctx);
}
TypesMgr::TypeId SymbolsVisitor::getTypeDecor(antlr4::ParserRuleContext *ctx) {
  return Decorations.getType(ctx);
}

// Setters for the necessary tree node attributes:
//   Scope and Type
void SymbolsVisitor::putScopeDecor(antlr4::ParserRuleContext *ctx, SymTable::ScopeId s) {
  Decorations.putScope(ctx, s);
}
void SymbolsVisitor::putTypeDecor(antlr4::ParserRuleContext *ctx, TypesMgr::TypeId t) {
  Decorations.putType(ctx, t);
}
