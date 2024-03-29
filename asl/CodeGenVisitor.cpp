//////////////////////////////////////////////////////////////////////
//
//    CodeGenVisitor - Walk the parser tree to do
//                     the generation of code
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

#include "CodeGenVisitor.h"

#include "antlr4-runtime.h"

#include "../common/TypesMgr.h"
#include "../common/SymTable.h"
#include "../common/TreeDecoration.h"
#include "../common/code.h"

#include <string>
#include <cstddef>    // std::size_t

// uncomment the following line to enable debugging messages with DEBUG*
// #define DEBUG_BUILD
#include "../common/debug.h"

// using namespace std;


// Constructor
CodeGenVisitor::CodeGenVisitor(TypesMgr       & Types,
                               SymTable       & Symbols,
                               TreeDecoration & Decorations) :
  Types{Types},
  Symbols{Symbols},
  Decorations{Decorations} {
}

// Methods to visit each kind of node:
//
antlrcpp::Any CodeGenVisitor::visitProgram(AslParser::ProgramContext *ctx) {
  DEBUG_ENTER();
  code my_code;
  SymTable::ScopeId sc = getScopeDecor(ctx);
  Symbols.pushThisScope(sc);
  for (auto ctxFunc : ctx->function()) { 
    subroutine subr = visit(ctxFunc);
    my_code.add_subroutine(subr);
  }
  Symbols.popScope();
  DEBUG_EXIT();
  return my_code;
}

antlrcpp::Any CodeGenVisitor::visitFunction(AslParser::FunctionContext *ctx) {
  DEBUG_ENTER();
  SymTable::ScopeId sc = getScopeDecor(ctx);
  Symbols.pushThisScope(sc);
  subroutine subr(ctx->ID()->getText());
  codeCounters.reset();
  if(ctx->basic_type()) subr.add_param("_result");
  if(ctx->parameters()) {
    std::vector<var> && params = visit(ctx->parameters());
    for (auto & par : params) {
      subr.add_param(par.name);
    }
  }

  std::vector<var> && lvars = visit(ctx->declarations());
  for (auto & onevar : lvars) {
    subr.add_var(onevar);
  }
  instructionList && code = visit(ctx->statements());
  code = code || instruction::RETURN();
  subr.set_instructions(code);
  Symbols.popScope();
  DEBUG_EXIT();
  return subr;
}

antlrcpp::Any CodeGenVisitor::visitParameters(AslParser::ParametersContext *ctx) {
  DEBUG_ENTER();
  std::vector<var> pvars;
  TypesMgr::TypeId t;
  std::size_t size;
  for(uint i = 0; i< ctx->ID().size() ; ++i){
    t = getTypeDecor(ctx->type(i));
    size = Types.getSizeOfType(t);
    pvars.push_back(var{ctx->ID(i)->getText(), size});
  }
  DEBUG_EXIT();
  return pvars;
}

antlrcpp::Any CodeGenVisitor::visitDeclarations(AslParser::DeclarationsContext *ctx) {
  DEBUG_ENTER();
  std::vector<var> lvars;
  for (auto & varDeclCtx : ctx->variable_decl()) {
    //multideclarations
    std::vector<var> decvars = visit(varDeclCtx);
    for (auto v : decvars) {
      lvars.push_back(v);
    }
  }
  DEBUG_EXIT();
  return lvars;
}

antlrcpp::Any CodeGenVisitor::visitVariable_decl(AslParser::Variable_declContext *ctx) {
  DEBUG_ENTER();
  TypesMgr::TypeId   t1 = getTypeDecor(ctx->type());
  std::size_t      size = Types.getSizeOfType(t1);
  std::vector<var> decvars;
  for(auto v : ctx->ID()) {
    decvars.push_back(var{v->getText(), size});
  }

  DEBUG_EXIT();
  return decvars;
}

antlrcpp::Any CodeGenVisitor::visitStatements(AslParser::StatementsContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  for (auto stCtx : ctx->statement()) {
    instructionList && codeS = visit(stCtx);
    code = code || codeS;
  }
  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitAssignStmt(AslParser::AssignStmtContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  CodeAttribs     && codAtsE1 = visit(ctx->left_expr());
  std::string           addr1 = codAtsE1.addr;
  std::string           offs1 = codAtsE1.offs;
  instructionList &     code1 = codAtsE1.code;
  TypesMgr::TypeId t1 = getTypeDecor(ctx->left_expr()->ident());
  CodeAttribs     && codAtsE2 = visit(ctx->expr());
  std::string           addr2 = codAtsE2.addr;
  std::string           offs2 = codAtsE2.offs;
  instructionList &     code2 = codAtsE2.code;
  TypesMgr::TypeId t2 = getTypeDecor(ctx->expr());

  if(Types.isArrayTy(t1) and Types.isArrayTy(t2)){
    bool isLocal1 = Symbols.isLocalVarClass(addr1);
    bool isLocal2  = Symbols.isLocalVarClass(addr2);

    std::string tempAddr1 = "%"+codeCounters.newTEMP();
    std::string tempAddr2  = "%"+codeCounters.newTEMP();

    if (not isLocal1) code = code || instruction::LOAD(tempAddr1, addr1);
    if (not isLocal2)  code = code || instruction::LOAD(tempAddr2, addr2);

    std::string tempIndex  = "%"+codeCounters.newTEMP();
    std::string tempIncrem = "%"+codeCounters.newTEMP();
    std::string tempSize   = "%"+codeCounters.newTEMP();
    std::string tempOffset = "%"+codeCounters.newTEMP();
    std::string tempOffHld = "%"+codeCounters.newTEMP();
    std::string tempCompar = "%"+codeCounters.newTEMP();
    std::string tempValue  = "%"+codeCounters.newTEMP();

    std::string labelWhile = "while"+codeCounters.newLabelWHILE();
    std::string labelEndWhile = "end"+labelWhile;

    code = code || instruction::ILOAD(tempIndex, "0");
    code = code || instruction::ILOAD(tempIncrem, "1");
    code = code || instruction::ILOAD(tempSize, std::to_string(Types.getArraySize(Symbols.getType(addr1))));
    code = code || instruction::ILOAD(tempOffset, "1");

    code = code || instruction::LABEL(labelWhile);
    code = code || instruction::LT(tempCompar, tempIndex, tempSize);
    code = code || instruction::FJUMP(tempCompar, labelEndWhile);
    code = code || instruction::MUL(tempOffHld, tempOffset, tempIndex);
    code = code || instruction::LOADX(tempValue, isLocal2 ? addr2 : tempAddr2, tempOffHld);
    code = code || instruction::XLOAD(isLocal1 ? addr1 : tempAddr1, tempOffHld, tempValue);
    code = code || instruction::ADD(tempIndex, tempIndex, tempIncrem);
    code = code || instruction::UJUMP(labelWhile);
    code = code || instruction::LABEL(labelEndWhile);
  }

  else if (Types.isArrayTy(t1) or Types.isArrayTy(t2)){
    // Left expr is array a[a1] = expr
    if (Types.isArrayTy(t1)) {
      std::string temp = "%"+codeCounters.newTEMP();
      code = code1 || code2
                   || instruction::XLOAD(addr1, offs1, addr2);
    }
    // Expr is array left_expr = a[a1]
    else if (Types.isArrayTy(t2)) {
      std::string temp = "%"+codeCounters.newTEMP();
      code = code1 || code2
                   || instruction::LOADX(temp, addr2, offs2)
                   || instruction::LOAD(addr1, temp);
    }
    // Both are arrays a[a1] = b[b1]
    else {
      std::string left_temp = "%"+codeCounters.newTEMP();
      std::string temp = "%"+codeCounters.newTEMP();
      code = code1 || instruction::LOADX(left_temp, addr1, offs1)
                   || code2
                   || instruction::LOADX(temp, addr2, offs2)
                   || instruction::LOAD(left_temp, temp);
    }
  }
  else code = code1 || code2 || instruction::LOAD(addr1, addr2);
  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitIfStmt(AslParser::IfStmtContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  CodeAttribs     && codAtsE = visit(ctx->expr());
  std::string          addr1 = codAtsE.addr;
  instructionList &    code1 = codAtsE.code;
  instructionList &&   code2 = visit(ctx->statements(0));

  std::string label = "if" + codeCounters.newLabelIF();
  std::string labelEndIf = "end"+label;

  if(ctx->ELSE()) {
    instructionList &&   code3 = visit(ctx->statements(1));
    std::string labelElse = "else"+label;

    code = code1 || instruction::FJUMP(addr1, labelElse) ||
           code2 || instruction::UJUMP(labelEndIf) || instruction::LABEL(labelElse) ||
           code3 || instruction::LABEL(labelEndIf);

  }
  else {
    code = code1 || instruction::FJUMP(addr1, labelEndIf) ||
           code2 || instruction::LABEL(labelEndIf);
  }

  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitWhileStmt(AslParser::WhileStmtContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  CodeAttribs     && codAtsE = visit(ctx->expr());
  std::string          addr1 = codAtsE.addr;
  instructionList &    code1 = codAtsE.code;
  instructionList &&   code2 = visit(ctx->statements());
  std::string label = "while" + codeCounters.newLabelWHILE();
  std::string labelEndWhile = "end" + label;
  code = instruction::LABEL(label) || code1 || instruction::FJUMP(addr1, labelEndWhile) ||
         code2 || instruction::UJUMP(label) || instruction::LABEL(labelEndWhile);
  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitProcCall(AslParser::ProcCallContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  // std::string name = ctx->ident()->ID()->getSymbol()->getText();
  std::string name = ctx->ident()->getText();
  TypesMgr::TypeId t = getTypeDecor(ctx->ident());

  // devuelve parametro _result
  if(not Types.isVoidTy(t)) code = code || instruction::PUSH();
  // si tiene parametros
  if(ctx->expr(0)){
    auto param_types = Types.getFuncParamsTypes(t);
    int i = 0;
    for(auto e : ctx->expr()){
      CodeAttribs     && codAtpar = visit(e);
      std::string         addrpar = codAtpar.addr;
      instructionList &   codepar = codAtpar.code;

      //check param int and expect float
      if(Types.isFloatTy(param_types[i]) and Types.isIntegerTy(getTypeDecor(e))){
        std::string tempF = "%"+codeCounters.newTEMP();
        code = code || codepar
                    || instruction::FLOAT(tempF, addrpar)
                    || instruction::PUSH(tempF);
      }
      //pass array address (reference parameter)
      else if (Types.isArrayTy(getTypeDecor(e))){
        std::string tempA = "%"+codeCounters.newTEMP();
        code = code || codepar 
                    || instruction::ALOAD(tempA,addrpar)
                    || instruction::PUSH(tempA);
      }
      else code = code || codepar || instruction::PUSH(addrpar);
      i++;
    }
    // call fname execute function fname.
    code = code || instruction::CALL(name);
    for (uint i = 0; i < (ctx->expr()).size(); ++i)
      code = code || instruction::POP();
  }
  // call fname execute function fname.
  else code = code || instruction::CALL(name);

  std::string temp = "%"+codeCounters.newTEMP();
  code = code || instruction::POP(temp);

  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitReadStmt(AslParser::ReadStmtContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs     && codAtsE = visit(ctx->left_expr());
  std::string          addr1 = codAtsE.addr;
  std::string          offs1 = codAtsE.offs;
  instructionList &    code1 = codAtsE.code;
  instructionList &     code = code1;

  TypesMgr::TypeId tE = getTypeDecor(ctx->left_expr());

  if(ctx->left_expr()->expr()){
    std::string temp = "%"+codeCounters.newTEMP();

    if (Types.isFloatTy(tE)) code = code || instruction::READF(temp);
    else if (Types.isCharacterTy(tE)) code = code || instruction::READC(temp);
    else code = code || instruction::READI(temp);

    code = code || instruction::XLOAD(addr1, offs1, temp);
  }
  else {
    if (Types.isFloatTy(tE)) code = code || instruction::READF(addr1);
    else if (Types.isCharacterTy(tE)) code = code || instruction::READC(addr1);
    else code = code || instruction::READI(addr1);
  }

  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitWriteExpr(AslParser::WriteExprContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs     && codAt1 = visit(ctx->expr());
  std::string         addr1 = codAt1.addr;
  // std::string         offs1 = codAt1.offs;
  instructionList &   code1 = codAt1.code;
  instructionList &    code = code1;

  TypesMgr::TypeId tid1 = getTypeDecor(ctx->expr());

  if(Types.isFloatTy(tid1)) code = code1 || instruction::WRITEF(addr1);
  else if(Types.isCharacterTy(tid1)) code = code1 || instruction::WRITEC(addr1);
  else code = code1 || instruction::WRITEI(addr1);

  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitWriteString(AslParser::WriteStringContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  std::string s = ctx->STRING()->getText();
  std::string temp = "%"+codeCounters.newTEMP();
  int i = 1;
  while (i < int(s.size())-1) {
    if (s[i] != '\\') {
      code = code ||
	     instruction::CHLOAD(temp, s.substr(i,1)) ||
	     instruction::WRITEC(temp);
      i += 1;
    }
    else {
      assert(i < int(s.size())-2);
      if (s[i+1] == 'n') {
        code = code || instruction::WRITELN();
        i += 2;
      }
      else if (s[i+1] == 't' or s[i+1] == '"' or s[i+1] == '\\') {
        code = code ||  instruction::CHLOAD(temp, s.substr(i,2)) ||
	                      instruction::WRITEC(temp);
        i += 2;
      }
      else {
        code = code ||  instruction::CHLOAD(temp, s.substr(i,1)) ||
	                      instruction::WRITEC(temp);
        i += 1;
      }
    }
  }
  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitRetStmt(AslParser::RetStmtContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  if(ctx->expr()) {
    CodeAttribs   && codAts = visit(ctx->expr());
    std::string         addr1 = codAts.addr;
    instructionList &   code1 = codAts.code;
    code = code1 || instruction::LOAD("_result", addr1);
  }
  DEBUG_EXIT();
  return code;  
}

antlrcpp::Any CodeGenVisitor::visitLeft_expr(AslParser::Left_exprContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs && codAts = visit(ctx->ident());
  std::string       addr = codAts.addr;
  instructionList & code = codAts.code;

  if(ctx->expr()){ //Array case
    CodeAttribs && codAt1 = visit(ctx->expr());
    std::string       addr1 = codAt1.addr;
    instructionList & code1 = codAt1.code;

    if (Symbols.isLocalVarClass(addr)) {
      code = code || code1;
      codAts = CodeAttribs(addr, addr1, code);
    }
    else {
      std::string tempA = "%"+codeCounters.newTEMP();
      code = code || code1
                  || instruction::LOAD(tempA, addr);
      codAts = CodeAttribs(tempA, addr1, code);
    }


  }
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitArray(AslParser::ArrayContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs && codAts = visit(ctx->ident());
  std::string       addr = codAts.addr;
  instructionList & code = codAts.code;

  CodeAttribs && codAt1 = visit(ctx->expr());
  std::string       addr1 = codAt1.addr;
  instructionList & code1 = codAt1.code;

  std::string temp = "%"+codeCounters.newTEMP();
    
  if (Symbols.isLocalVarClass(addr)) {
    code = code || code1
                || instruction::LOADX(temp, addr, addr1) ;
  } 
  else {
    std::string tempA = "%"+codeCounters.newTEMP();
    code = code || code1
                || instruction::LOAD(tempA, addr)
                || instruction::LOADX(temp, tempA, addr1) ;
  } 

  codAts = CodeAttribs(temp, "", code);

  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitArithmetic(AslParser::ArithmeticContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs     && codAt1 = visit(ctx->expr(0));
  std::string         addr1 = codAt1.addr;
  instructionList &   code1 = codAt1.code;
  CodeAttribs     && codAt2 = visit(ctx->expr(1));
  std::string         addr2 = codAt2.addr;
  instructionList &   code2 = codAt2.code;
  instructionList &&   code = code1 || code2;

  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr(0));
  TypesMgr::TypeId t2 = getTypeDecor(ctx->expr(1));
  //TypesMgr::TypeId  t = getTypeDecor(ctx);
  std::string temp = "%"+codeCounters.newTEMP();

  if(not Types.isFloatTy(t1) and not Types.isFloatTy(t2)) {
    if (ctx->MUL()) code = code || instruction::MUL(temp, addr1, addr2);
    else if (ctx->DIV())  code = code || instruction::DIV(temp, addr1, addr2);
    else if (ctx->MIN())  code = code || instruction::SUB(temp, addr1, addr2);
    else if (ctx->PLUS()) code = code || instruction::ADD(temp, addr1, addr2);
    else { //ctx->MOD()
      code = code || instruction::DIV(temp, addr1, addr2) 
                  || instruction::MUL(temp, temp, addr2) 
                  || instruction::SUB(temp, addr1, temp);
    }
  }
  else {
    std::string faddr1 = addr1;
    std::string faddr2 = addr2;
    if(Types.isIntegerTy(t1)) {
      faddr1 = "%"+codeCounters.newTEMP();
      code = code || instruction::FLOAT(faddr1,addr1);
    }
    else if(Types.isIntegerTy(t2)) {
      faddr2 = "%"+codeCounters.newTEMP();
      code = code || instruction::FLOAT(faddr2,addr2);
    }
    if (ctx->MUL()) code = code || instruction::FMUL(temp, faddr1, faddr2);
    else if (ctx->DIV())  code = code || instruction::FDIV(temp, faddr1, faddr2);
    else if (ctx->MIN())  code = code || instruction::FSUB(temp, faddr1, faddr2);
    else if (ctx->PLUS()) code = code || instruction::FADD(temp, faddr1, faddr2);
  }
  CodeAttribs codAts(temp, "", code);
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitRelational(AslParser::RelationalContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs     && codAt1 = visit(ctx->expr(0));
  std::string         addr1 = codAt1.addr;
  instructionList &   code1 = codAt1.code;
  CodeAttribs     && codAt2 = visit(ctx->expr(1));
  std::string         addr2 = codAt2.addr;
  instructionList &   code2 = codAt2.code;
  instructionList &&   code = code1 || code2;
  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr(0));
  TypesMgr::TypeId t2 = getTypeDecor(ctx->expr(1));
  // TypesMgr::TypeId  t = getTypeDecor(ctx);
  std::string temp = "%"+codeCounters.newTEMP();

  // INT, BOOL, CHAR
  if(not Types.isFloatTy(t1) and not Types.isFloatTy(t2)) {
    if (ctx->EQUAL()) code = code || instruction::EQ(temp, addr1, addr2);
    else if (ctx->NEQ())  code = code || instruction::EQ(temp, addr1, addr2) || instruction::NOT(temp, temp);
    else if(ctx->LT())  code = code || instruction::LT(temp, addr1, addr2);
    else if(ctx->LTE()) code = code || instruction::LE(temp, addr1, addr2);
    else if(ctx->GT())  code = code || instruction::LT(temp, addr2, addr1);
    else if(ctx->GTE()) code = code || instruction::LE(temp, addr2, addr1);
  }
  else {  // FLOAT
    std::string faddr1 = addr1;
    std::string faddr2 = addr2;
    if(Types.isIntegerTy(t1)) {
      faddr1 = "%"+codeCounters.newTEMP();
      code = code || instruction::FLOAT(faddr1,addr1);
    }
    else if(Types.isIntegerTy(t2)){
      faddr2 = "%"+codeCounters.newTEMP();
      code = code || instruction::FLOAT(faddr2,addr2);
    }

    if (ctx->EQUAL()) code = code || instruction::FEQ(temp, faddr1, faddr2);
    else if (ctx->NEQ())  code = code || instruction::FEQ(temp, faddr1, faddr2) || instruction::NOT(temp, temp);
    else if(ctx->LT())  code = code || instruction::FLT(temp, faddr1, faddr2);
    else if(ctx->LTE()) code = code || instruction::FLE(temp, faddr1, faddr2);
    else if(ctx->GT())  code = code || instruction::FLT(temp,faddr2,faddr1);
    else if(ctx->GTE()) code = code || instruction::FLE(temp,faddr2,faddr1);
  }

  CodeAttribs codAts(temp, "", code);
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitUnary(AslParser::UnaryContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs     && codAt1 = visit(ctx->expr());
  std::string         addr1 = codAt1.addr;
  instructionList &   code1 = codAt1.code;
  instructionList &    code = code1;
  std::string temp = "%"+codeCounters.newTEMP();

  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr());
  if(ctx->NOT()) code = code1 || instruction::NOT(temp, addr1);
  else if(ctx->MIN())
    if(not Types.isFloatTy(t1)) code = code1 || instruction::NEG(temp, addr1);
    else code = code1 || instruction::FNEG(temp, addr1);
  else code = code1 || instruction::LOAD(temp, addr1);

  CodeAttribs codAts(temp, "", code);
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitParenthesis(AslParser::ParenthesisContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs   && codAts = visit(ctx->expr());
  //std::string         addr1 = codAts.addr;
  //instructionList &   code1 = codAts.code;
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitValue(AslParser::ValueContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  std::string temp = "%"+codeCounters.newTEMP();
  if(ctx->INTVAL()) code = instruction::ILOAD(temp, ctx->getText());
  else if(ctx->FLOATVAL()) code = instruction::FLOAD(temp, ctx->getText());
  else if(ctx->CHARVAL()) {
    std::string ch = ctx->getText();
    code = instruction::CHLOAD(temp, ch.substr(1,ch.size()-2));
  }
  else code = instruction::LOAD(temp, (ctx->getText() == "true") ? "1" : "0");
  CodeAttribs codAts(temp, "", code);
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitCallFunc(AslParser::CallFuncContext *ctx) {
  DEBUG_ENTER();

  CodeAttribs && codAt1 = visit(ctx->ident());
  std::string         addr1 = codAt1.addr;
  instructionList &   code1 = codAt1.code;
  instructionList &   code = code1;
  TypesMgr::TypeId t = getTypeDecor(ctx->ident());

  //tiene return 
  if(not Types.isVoidTy(t)) code = code || instruction::PUSH();
  //tiene parametros
  if(ctx->expr(0)) {
    auto param_types = Types.getFuncParamsTypes(t);
    int i = 0;
    for(auto e : ctx->expr()){
      CodeAttribs     && codAtpar = visit(e);
      std::string         addrpar = codAtpar.addr;
      instructionList &   codepar = codAtpar.code;

      //check param int and expect float
      if(Types.isFloatTy(param_types[i]) and Types.isIntegerTy(getTypeDecor(e))){
        std::string tempF = "%"+codeCounters.newTEMP();
        code = code || codepar
                    || instruction::FLOAT(tempF, addrpar)
                    || instruction::PUSH(tempF);
      }
      //pass array address (reference parameter)
      else if (Types.isArrayTy(getTypeDecor(e))){
        std::string tempA = "%"+codeCounters.newTEMP();
        code = code || codepar 
                    || instruction::ALOAD(tempA,addrpar)
                    || instruction::PUSH(tempA);
      }
      else code = code || codepar || instruction::PUSH(addrpar);
      i++;
    }
    // call fname execute function fname.
    code = code || instruction::CALL(addr1);
    for (uint i = 0; i < (ctx->expr()).size(); ++i)
      code = code || instruction::POP();
  }

  else {
    code = code || instruction::CALL(addr1);
  }

  std::string temp = "%"+codeCounters.newTEMP();
  code = code || instruction::POP(temp);

  CodeAttribs codAts(temp, "", code);

  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitExprIdent(AslParser::ExprIdentContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs && codAts = visit(ctx->ident());
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitLogical(AslParser::LogicalContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs     && codAt1 = visit(ctx->expr(0));
  std::string         addr1 = codAt1.addr;
  instructionList &   code1 = codAt1.code;
  CodeAttribs     && codAt2 = visit(ctx->expr(1));
  std::string         addr2 = codAt2.addr;
  instructionList &   code2 = codAt2.code;
  instructionList &&   code = code1 || code2;
  //TypesMgr::TypeId t1 = getTypeDecor(ctx->expr(0));
  //TypesMgr::TypeId t2 = getTypeDecor(ctx->expr(1));
  // TypesMgr::TypeId  t = getTypeDecor(ctx);
  std::string temp = "%"+codeCounters.newTEMP();

  if(ctx->AND()) code = code || instruction::AND(temp, addr1, addr2);
  else code = code || instruction::OR(temp, addr1, addr2);

  CodeAttribs codAts(temp, "", code);
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitIdent(AslParser::IdentContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs codAts(ctx->ID()->getText(), "", instructionList());
  DEBUG_EXIT();
  return codAts;
}


// Getters for the necessary tree node atributes:
//   Scope and Type
SymTable::ScopeId CodeGenVisitor::getScopeDecor(antlr4::ParserRuleContext *ctx) const {
  return Decorations.getScope(ctx);
}
TypesMgr::TypeId CodeGenVisitor::getTypeDecor(antlr4::ParserRuleContext *ctx) const {
  return Decorations.getType(ctx);
}


// Constructors of the class CodeAttribs:
//
CodeGenVisitor::CodeAttribs::CodeAttribs(const std::string & addr,
					 const std::string & offs,
					 instructionList & code) :
  addr{addr}, offs{offs}, code{code} {
}

CodeGenVisitor::CodeAttribs::CodeAttribs(const std::string & addr,
					 const std::string & offs,
					 instructionList && code) :
  addr{addr}, offs{offs}, code{code} {
}
