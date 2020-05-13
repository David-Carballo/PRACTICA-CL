#include "CodeGenListener.h"

#include "antlr4-runtime.h"

#include "../common/TypesMgr.h"
#include "../common/SymTable.h"
#include "../common/TreeDecoration.h"
#include "../common/code.h"

#include <cstddef>    // size_t

// uncomment the following line to enable debugging messages with DEBUG*
// #define DEBUG_BUILD
#include "../common/debug.h"

using namespace std;


// Constructor
CodeGenListener::CodeGenListener(TypesMgr &Types, SymTable &Symbols, 
								 TreeDecoration &Decorations, code &Code) :
	Types{Types}, Symbols{Symbols}, Decorations{Decorations}, Code{Code} {

}

void CodeGenListener::enterProgram(AslParser::ProgramContext *ctx) {
	DEBUG_ENTER();
	SymTable::ScopeId sc = getScopeDecor(ctx);
	Symbols.pushThisScope(sc);
}
void CodeGenListener::exitProgram(AslParser::ProgramContext *ctx) {
	Symbols.popScope();
	DEBUG_EXIT();
}

void CodeGenListener::enterFunction(AslParser::FunctionContext *ctx) {
	DEBUG_ENTER();
	subroutine subr(ctx->ID()->getText());

	if (ctx->funcType()) {
		subr.add_param("_result");
	}
	if (ctx->funcParams()) {
		for (auto ct:ctx->funcParams()->ID()) {
			subr.add_param(ct->getText());
		}
	}
	Code.add_subroutine(subr);
	SymTable::ScopeId sc = getScopeDecor(ctx);
	Symbols.pushThisScope(sc);
	codeCounters.reset();
}
void CodeGenListener::exitFunction(AslParser::FunctionContext *ctx) {
	subroutine & subrRef = Code.get_last_subroutine();
	instructionList code = getCodeDecor(ctx->statements());
	code = code || instruction::RETURN();
	subrRef.set_instructions(code);
	Symbols.popScope();
	DEBUG_EXIT();
}

void CodeGenListener::enterDeclarations(AslParser::DeclarationsContext *ctx) { DEBUG_ENTER(); }
void CodeGenListener::exitDeclarations(AslParser::DeclarationsContext *ctx) { DEBUG_EXIT(); }

void CodeGenListener::enterVariable_decl(AslParser::Variable_declContext *ctx) {
  DEBUG_ENTER();
}
void CodeGenListener::exitVariable_decl(AslParser::Variable_declContext *ctx) {
	subroutine       & subrRef = Code.get_last_subroutine();
    TypesMgr::TypeId        t1 = getTypeDecor(ctx->typeR());
    size_t           size = Types.getSizeOfType(t1);
    for (auto varCt : ctx->ID()) {
        subrRef.add_var(varCt->getText(), size);
    }
}

void CodeGenListener::enterTypeR(AslParser::TypeRContext *ctx) {}
void CodeGenListener::exitTypeR(AslParser::TypeRContext *ctx) {}

void CodeGenListener::enterType(AslParser::TypeContext *ctx) {}
void CodeGenListener::exitType(AslParser::TypeContext *ctx) {}

void CodeGenListener::enterStatements(AslParser::StatementsContext *ctx) {
  DEBUG_ENTER();
}
void CodeGenListener::exitStatements(AslParser::StatementsContext *ctx) {
	instructionList code;
	for (auto st : ctx->statement()) {
		instructionList codeSt = getCodeDecor(st);
		code = code || codeSt;
	}
	putCodeDecor(ctx, code);
	DEBUG_EXIT();
}

void CodeGenListener::enterAssignStmt(AslParser::AssignStmtContext *ctx) { DEBUG_ENTER(); }
void CodeGenListener::exitAssignStmt(AslParser::AssignStmtContext *ctx) {
	instructionList  code;
	// left_expr = ...

	string          addr1 = getAddrDecor(ctx->left_expr());
	string          offs1 = getOffsetDecor(ctx->left_expr());
	instructionList code1 = getCodeDecor(ctx->left_expr());
	TypesMgr::TypeId   t1 = getTypeDecor(ctx->left_expr());

	// ... = expr
	string          addr2 = getAddrDecor(ctx->expr());
	string          offs2 = getOffsetDecor(ctx->expr());
	instructionList code2 = getCodeDecor(ctx->expr());
	TypesMgr::TypeId   t2 = getTypeDecor(ctx->expr());

	// a[10] = ...
	if (ctx->left_expr()->arrayConstruct()) {
		if (not Symbols.isParameterClass(ctx->left_expr()->arrayConstruct()->ident()->ID()->getText())) {
			code = code1 || code2;
			code = code  || instruction::XLOAD(addr1, offs1, addr2);	
		}
		else {
			string tmp = "%"+codeCounters.newTEMP();
			code = code1 || code2;
			code = code  || instruction::LOAD(tmp, addr1);
			code = code  || instruction::XLOAD(tmp, offs1, addr2);
		}
	}
	// a = .. on a es un array
	else if (Types.isArrayTy(t1)){
		int size = Types.getArraySize(t1);
		instructionList codeaux = instructionList();
		size_t s = Types.getSizeOfType(Types.getArrayElemType(t1));
		string index = "%"+codeCounters.newTEMP();
		string temp1 = "%"+codeCounters.newTEMP();
		string temp2 = "%"+codeCounters.newTEMP();
		codeaux = codeaux || instruction::ILOAD(index, "0");
		codeaux = codeaux || instruction::ILOAD(temp1, to_string(s));
		//ctx->left_expr()->ident()->ID()->getText()
		// adress(A[i]) = Base(A) + i * sizeElem(A)
		if (not Symbols.isParameterClass(addr1) and not Symbols.isParameterClass(addr2)) {
			for(int i = 0; i < size; i++){
				codeaux = codeaux || instruction::LOADX(temp2, addr2, index); // temp2 = addr2[index]
				codeaux = codeaux || instruction::XLOAD(addr1, index, temp2); // addr1[index] = temp2
				codeaux = codeaux || instruction::ADD(index, index, temp1);   // index = index + size_t
			}
		}
		else if (not Symbols.isParameterClass(addr1)) {
			string temp3 = "%"+codeCounters.newTEMP();
			for(int i = 0; i < size; i++){
				codeaux = codeaux || instruction::LOAD(temp3, addr2);
				codeaux = codeaux || instruction::LOADX(temp2, temp3, index); // temp2 = addr2[index]
				codeaux = codeaux || instruction::XLOAD(addr1, index,temp2);  // addr1[index] = temp2
				codeaux = codeaux || instruction::ADD(index, index, temp1);
			}
		}
		else if (not Symbols.isParameterClass(addr2)){
			string temp3 = "%"+codeCounters.newTEMP();
			for(int i = 0; i < size; i++){
				codeaux = codeaux || instruction::LOAD(temp3, addr1);
				codeaux = codeaux || instruction::LOADX(temp2, addr2,index);
				codeaux = codeaux || instruction::XLOAD(temp3, index,temp2);
				codeaux = codeaux || instruction::ADD(index, index, temp1);
			}
		}
		else {
			string temp3 = "%"+codeCounters.newTEMP();
			for(int i = 0; i < size; i++){
				codeaux = codeaux || instruction::LOAD(temp3, addr2);
				codeaux = codeaux || instruction::LOADX(temp2, temp3,index);
				codeaux = codeaux || instruction::LOAD(temp3, addr1);
				codeaux = codeaux || instruction::XLOAD(temp3, index,temp2);
				codeaux = codeaux || instruction::ADD(index, index, temp1);
			}
		}
		code = code || codeaux;
	}
	else {
		code = code1 || code2 || instruction::LOAD(addr1, addr2);
	}
	putCodeDecor(ctx, code);
	DEBUG_EXIT();
}

void CodeGenListener::enterIfStmt(AslParser::IfStmtContext *ctx) {
  DEBUG_ENTER();
}
void CodeGenListener::exitIfStmt(AslParser::IfStmtContext *ctx) {
	instructionList  code  = instructionList();
	string           addr1 = getAddrDecor(ctx->expr());
	instructionList  code1 = getCodeDecor(ctx->expr());
	instructionList  code2 = getCodeDecor(ctx->statements());
	string           label = codeCounters.newLabelIF();
	string      labelEndIf = "endif"+label;
	if (!ctx->elseStat()) {
		code = code  || code1;
		code = code  || instruction::FJUMP(addr1, labelEndIf);
		code = code  || code2 ;
		code = code  || instruction::LABEL(labelEndIf);
	}
	else {
		string labelElse = "else"+label;
		instructionList  code3 = getCodeDecor(ctx->elseStat()->statements());
		code = code1 || instruction::FJUMP(addr1, labelElse);
		code = code  || code2 || instruction::UJUMP(labelEndIf);
		code = code  || instruction::LABEL(labelElse);
		code = code  || code3 || instruction::LABEL(labelEndIf);;
	}
	putCodeDecor(ctx, code);
	DEBUG_EXIT();
}

void CodeGenListener::enterWhileStmt(AslParser::WhileStmtContext *ctx) {}
void CodeGenListener::exitWhileStmt(AslParser::WhileStmtContext *ctx) {
	instructionList  code  = instructionList();
	string           addr  = getAddrDecor(ctx->expr());
	instructionList  code1 = getCodeDecor(ctx->expr());
	instructionList  code2 = getCodeDecor(ctx->statements());
	string           label = codeCounters.newLabelWHILE();
	string   labelEndWhile = "endWhile"+label;
	string      labelWhile = "while"+label;

	code = code || instruction::LABEL(labelWhile);
	code = code || code1;
	code = code || instruction::FJUMP(addr, labelEndWhile);
	code = code || code2 || instruction::UJUMP(labelWhile);
	code = code || instruction::LABEL(labelEndWhile);

	putCodeDecor(ctx, code);
}

void CodeGenListener::enterProcCall(AslParser::ProcCallContext *ctx) {
  DEBUG_ENTER();
}
void CodeGenListener::exitProcCall(AslParser::ProcCallContext *ctx) {
    DEBUG_EXIT();

    string          addr = getAddrDecor(ctx->funcConstruct());
    instructionList code = getCodeDecor(ctx->funcConstruct());
    putCodeDecor(ctx, code);
    putAddrDecor(ctx, addr);
}

void CodeGenListener::enterReadStmt(AslParser::ReadStmtContext *ctx) {
    DEBUG_ENTER();
}
void CodeGenListener::exitReadStmt(AslParser::ReadStmtContext *ctx) {
    instructionList  code;
    TypesMgr::TypeId     t = getTypeDecor(ctx->left_expr());
    string           addr1 = getAddrDecor(ctx->left_expr());
    instructionList  code1 = getCodeDecor(ctx->left_expr());
    string           offs1 = getOffsetDecor(ctx->left_expr()->arrayConstruct());
    bool           isArray = false;
    string tmp;
    if (ctx->left_expr()->arrayConstruct()) {
    	tmp = addr1;
    	addr1 = "%"+codeCounters.newTEMP();
    	isArray = true;
    }
    if (Types.isCharacterTy(t))
    	code = code1 || instruction::READC(addr1);
    else if (Types.isFloatTy(t))
    	code = code1 || instruction::READF(addr1);
    else if (Types.isIntegerTy(t) or Types.isBooleanTy(t))
    	code = code1 || instruction::READI(addr1);
    if (isArray) {
    	// lectura dels arrays a[a1] = ...
    	string addrf = getAddrDecor(ctx->left_expr()->arrayConstruct()->ident());
    	if (not Symbols.isParameterClass(addrf)) {
    		code = code || instruction::XLOAD(tmp, offs1, addr1);
    	}
    	else {
    		string tmp2 = "%"+codeCounters.newTEMP();
    		code = code || instruction::LOAD(tmp2, addrf);
    		code = code || instruction::XLOAD(tmp2, offs1, addr1);
    	}

    }
    putCodeDecor(ctx, code);
    DEBUG_EXIT();
}

void CodeGenListener::enterWriteExpr(AslParser::WriteExprContext *ctx) { DEBUG_ENTER(); }
void CodeGenListener::exitWriteExpr(AslParser::WriteExprContext *ctx) {
    // string     offs1 = getOffsetDecor(ctx->expr());
    TypesMgr::TypeId   t = getTypeDecor(ctx->expr());
    string         addr1 = getAddrDecor(ctx->expr());
    instructionList code = getCodeDecor(ctx->expr());
    if (Types.isCharacterTy(t))
        code = code || instruction::WRITEC(addr1);
    else if (Types.isFloatTy(t))
        code = code || instruction::WRITEF(addr1);
    else
        code = code || instruction::WRITEI(addr1);

    putCodeDecor(ctx, code);
    DEBUG_EXIT();
}

void CodeGenListener::enterWriteString(AslParser::WriteStringContext *ctx) {
    DEBUG_ENTER();
}
void CodeGenListener::exitWriteString(AslParser::WriteStringContext *ctx) {
	instructionList code;
	string s = ctx->STRING()->getText();
	string temp = "%"+codeCounters.newTEMP();
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
				code = code ||
				instruction::CHLOAD(temp, s.substr(i,2)) ||
				instruction::WRITEC(temp);
				i += 2;
			}
			else {
				code = code ||
				instruction::CHLOAD(temp, s.substr(i,1)) ||
				instruction::WRITEC(temp);
				i += 1;
			}
		}
	}
	putCodeDecor(ctx, code);
	DEBUG_EXIT();
}

void CodeGenListener::enterRetFunc(AslParser::RetFuncContext *ctx) {}
void CodeGenListener::exitRetFunc(AslParser::RetFuncContext *ctx) {
	if (ctx->expr()) {
		instructionList code = getCodeDecor(ctx->expr());
    	string          temp = getAddrDecor(ctx->expr());
    	code = code || instruction::LOAD("_result", temp);
    	putCodeDecor(ctx, code);
	}
}

void CodeGenListener::enterLeft_expr(AslParser::Left_exprContext *ctx) {
  DEBUG_ENTER();
}
void CodeGenListener::exitLeft_expr(AslParser::Left_exprContext *ctx) {
	if (ctx->arrayConstruct()) {
		putAddrDecor(ctx, getAddrDecor(ctx->arrayConstruct()));
		putOffsetDecor(ctx, getOffsetDecor(ctx->arrayConstruct()));
		putCodeDecor(ctx, getCodeDecor(ctx->arrayConstruct()));
	}
	else {
		putAddrDecor(ctx, getAddrDecor(ctx->ident()));
		putOffsetDecor(ctx, getOffsetDecor(ctx->ident()));
		putCodeDecor(ctx, getCodeDecor(ctx->ident()));
	}
}

void CodeGenListener::enterFuncExpr(AslParser::FuncExprContext *ctx) {}
void CodeGenListener::exitFuncExpr(AslParser::FuncExprContext *ctx) {    
    string          addr = getAddrDecor(ctx->funcConstruct());
    instructionList code = getCodeDecor(ctx->funcConstruct());
    putCodeDecor(ctx, code);
    putAddrDecor(ctx, addr);
    
}

void CodeGenListener::enterFuncConstruct(AslParser::FuncConstructContext *ctx) {}
void CodeGenListener::exitFuncConstruct(AslParser::FuncConstructContext *ctx) {
    instructionList code = instructionList();
    string          name = ctx->ident()->ID()->getText();
    string          addr = getAddrDecor(ctx->ident());
    TypesMgr::TypeId   t = getTypeDecor(ctx->ident());

    if (not Types.isVoidFunction(t)) {
    	code = code || instruction::PUSH();
    }

    if (ctx->exprFunc()) {
    	int i = 0;
    	for (auto ct:ctx->exprFunc()->expr()) {
    		// Per cada parametre obtenim el codi i l'adreça i fem un push
    		instructionList codeParam = getCodeDecor(ct);
    		string              addrP = getAddrDecor(ct);
    		TypesMgr::TypeId     tPar = getTypeDecor(ct);
    		TypesMgr::TypeId     tCal = Types.getParameterType(t, i++);

    		if (Types.isFloatTy(tCal) and Types.isIntegerTy(tPar)) {
    			string tmpAddr = "%"+codeCounters.newTEMP();
    			codeParam = codeParam || instruction::FLOAT(tmpAddr, addrP);
    			addrP = tmpAddr;
    		}

    		if (Types.isArrayTy(tCal)) {
    			string tmpAddr = "%"+codeCounters.newTEMP();
    			codeParam = codeParam || instruction::ALOAD(tmpAddr, addrP);
    			addrP = tmpAddr;	
    		}

    		code = code || codeParam || instruction::PUSH(addrP);
    	}
   	    code = code || instruction::CALL(addr);
	    for (auto ct:ctx->exprFunc()->expr()) {
	    	code = code || instruction::POP();
	    }
		if (not Types.isVoidFunction(t)) {
			string tmpAddr = "%"+codeCounters.newTEMP();
	    	code = code || instruction::POP(tmpAddr);
	    	putAddrDecor(ctx, tmpAddr);
	    }    
	    putCodeDecor(ctx, code);
    }
    
    else {
    	code = code || instruction::CALL(name);
    	putCodeDecor(ctx, code);
    }
    DEBUG_EXIT();
}

void CodeGenListener::enterArrayExpr(AslParser::ArrayExprContext *ctx) {}
void CodeGenListener::exitArrayExpr(AslParser::ArrayExprContext *ctx) {
	instructionList  code = getCodeDecor(ctx->arrayConstruct());
	string           addr = getAddrDecor(ctx->arrayConstruct());
	string           offs = getOffsetDecor(ctx->arrayConstruct());
	//TypesMgr::TypeId type = getTypeDecor(ctx->arrayConstruct());

	string tmp = "%"+codeCounters.newTEMP();
	if (not Symbols.isParameterClass(ctx->arrayConstruct()->ident()->getText())) {
		code = code || instruction::LOADX(tmp, addr, offs);
	}
	else {
		code = code || instruction::LOAD(tmp, addr);
		code = code || instruction::LOADX(tmp, tmp, offs);
	}

	putAddrDecor(ctx, tmp);
	putCodeDecor(ctx, code);
	putOffsetDecor(ctx, offs);
}

void CodeGenListener::enterArrayConstruct(AslParser::ArrayConstructContext *ctx) {}
void CodeGenListener::exitArrayConstruct(AslParser::ArrayConstructContext *ctx) {
	string           addr  = getAddrDecor(ctx->expr());
	string           addi  = getAddrDecor(ctx->ident());
	instructionList  code1 = getCodeDecor(ctx->expr());
	instructionList  code2 = getCodeDecor(ctx->ident());
	TypesMgr::TypeId type  = getTypeDecor(ctx->ident());
	size_t           size  = Types.getSizeOfType(Types.getArrayElemType(type));
	instructionList  code  = code1 || code2;
	
	string off = "%"+codeCounters.newTEMP();
	code = code || instruction::ILOAD(off, to_string(size));
	code = code || instruction::MUL(off, off, addr);

	putCodeDecor(ctx, code);
	putAddrDecor(ctx, addi);
	putOffsetDecor(ctx, off);
}

void CodeGenListener::enterArithmetic(AslParser::ArithmeticContext *ctx) {
  DEBUG_ENTER();
}
void CodeGenListener::exitArithmetic(AslParser::ArithmeticContext *ctx) {
    string          addr1 = getAddrDecor(ctx->expr(0));
    string          addr2 = getAddrDecor(ctx->expr(1));
 
    instructionList code1 = getCodeDecor(ctx->expr(0));
    instructionList code2 = getCodeDecor(ctx->expr(1));
 
    TypesMgr::TypeId   t1 = getTypeDecor(ctx->expr(0));
    TypesMgr::TypeId   t2 = getTypeDecor(ctx->expr(1));
 
    instructionList code  = code1 || code2;
    string temp = "%"+codeCounters.newTEMP();

    if (Types.isIntegerTy(getTypeDecor(ctx))){
        //Both parameters ARE integers
        if (ctx->MUL())
            code = code || instruction::MUL(temp, addr1, addr2);
        else if (ctx->PLUS())
            code = code || instruction::ADD(temp, addr1, addr2);
        else if (ctx->MINUS())
            code = code || instruction::SUB(temp, addr1, addr2);
        else if (ctx->DIV())
            code = code || instruction::DIV(temp, addr1, addr2);
        else if (ctx->MOD()) {
            string temp1 = "%"+codeCounters.newTEMP();
            string temp2 = "%"+codeCounters.newTEMP();
            code = code || instruction::DIV(temp1, addr1, addr2);
            code = code || instruction::MUL(temp2, temp1, addr2);
            code = code || instruction::SUB(temp, addr1, temp2);
        }
    } 
    else {
        string faddr1, faddr2;

        if (Types.isIntegerTy(t1)){
            faddr1 = "%"+codeCounters.newTEMP();
            faddr2 = addr2;
            code = code || instruction::FLOAT(faddr1, addr1);
        }
        else if (Types.isIntegerTy(t2) ){
            faddr1 = addr1;
            faddr2 = "%"+codeCounters.newTEMP();
            code = code || instruction::FLOAT(faddr2, addr2);
        }
        else {
            faddr1 = addr1;
            faddr2 = addr2;
        }

        if (ctx->MUL())
            code = code || instruction::FMUL(temp, faddr1, faddr2);
        else if (ctx->PLUS())
            code = code || instruction::FADD(temp, faddr1, faddr2);
        else if (ctx->MINUS())
            code = code || instruction::FSUB(temp, faddr1, faddr2);
        else if (ctx->DIV())
            code = code || instruction::FDIV(temp, faddr1, faddr2);

    }
    putAddrDecor(ctx, temp);
    putOffsetDecor(ctx, "");
    putCodeDecor(ctx, code);
}

void CodeGenListener::enterRelational(AslParser::RelationalContext *ctx) {
  DEBUG_ENTER();
}
void CodeGenListener::exitRelational(AslParser::RelationalContext *ctx) {
    string     addr1 = getAddrDecor(ctx->expr(0));
    instructionList code1 = getCodeDecor(ctx->expr(0));
    string     addr2 = getAddrDecor(ctx->expr(1));
    instructionList code2 = getCodeDecor(ctx->expr(1));
    instructionList code  = code1 || code2;
    TypesMgr::TypeId t1 = getTypeDecor(ctx->expr(0));
    TypesMgr::TypeId t2 = getTypeDecor(ctx->expr(1));

    string temp = "%"+codeCounters.newTEMP();
    if ((Types.isIntegerTy(t1) and Types.isIntegerTy(t2)) or (Types.isCharacterTy(t1) and Types.isCharacterTy(t2))) {
        if (ctx->LE()) {
            code = code || instruction::LE(temp, addr1, addr2);
        }
        else if (ctx->EQ()) {
            code = code || instruction::EQ(temp, addr1, addr2);
        }
        else if (ctx->NEQ()) {
            string temp1 = "%"+codeCounters.newTEMP();
            code = code || instruction::EQ(temp1, addr1, addr2);
            code = code || instruction::NOT(temp, temp1);
        }
        else if (ctx->LT()) {
            code = code || instruction::LT(temp, addr1, addr2);
        }
        else if (ctx->GE()) {
            string temp1 = "%"+codeCounters.newTEMP();
            code = code || instruction::LT(temp1, addr1, addr2);
            code = code || instruction::NOT(temp, temp1);
        }
        else { //ctx->GT
            string temp1 = "%"+codeCounters.newTEMP();
            code = code || instruction::LE(temp1, addr1, addr2);
            code = code || instruction::NOT(temp, temp1);
        }
    }
    else if (Types.isBooleanTy(t1) and Types.isBooleanTy(t2)) {
        if (ctx->EQ()) {
            code = code || instruction::EQ(temp, addr1, addr2);
        }
        else {
            string temp1 = "%"+codeCounters.newTEMP();
            code = code || instruction::EQ(temp1, addr1, addr2);
            code = code || instruction::NOT(temp, temp1);
        }
    }
    else {
        string faddr1, faddr2;
        if (Types.isIntegerTy(t1)) {
            faddr1 = "%"+codeCounters.newTEMP();
            faddr2 = addr2;
            code = code || instruction::FLOAT(faddr1, addr1);
        }
        else if (Types.isIntegerTy(t2)) {
            faddr2 = "%"+codeCounters.newTEMP();
            faddr1 = addr1;
            code = code || instruction::FLOAT(faddr2, addr2);   
        }
        else {
            faddr2 = addr2;
            faddr1 = addr1;
        }
        if (ctx->LE()) {
            code = code || instruction::FLE(temp, faddr1, faddr2);
        }
        else if (ctx->EQ()) {
            code = code || instruction::FEQ(temp, faddr1, faddr2);
        }
        else if (ctx->NEQ()) {
            string temp1 = "%"+codeCounters.newTEMP();
            code = code || instruction::FEQ(temp1, faddr1, faddr2);
            code = code || instruction::NOT(temp, temp1);
        }
        else if (ctx->LT()) {
            code = code || instruction::FLT(temp, faddr1, faddr2);
        }
        else if (ctx->GE()) {
            string temp1 = "%"+codeCounters.newTEMP();
            code = code || instruction::FLT(temp1, faddr1, faddr2);
            code = code || instruction::NOT(temp, temp1);
        }
        else { //ctx->GT
            string temp1 = "%"+codeCounters.newTEMP();
            code = code || instruction::FLE(temp1, faddr1, faddr2);
            code = code || instruction::NOT(temp, temp1);
        }
    }

    putAddrDecor(ctx, temp);
    putOffsetDecor(ctx, "");
    putCodeDecor(ctx, code);
    DEBUG_EXIT();
}

void CodeGenListener::enterBooleanExpr(AslParser::BooleanExprContext *ctx) {
  DEBUG_ENTER();
}
void CodeGenListener::exitBooleanExpr(AslParser::BooleanExprContext *ctx) {
    string          addr1 = getAddrDecor(ctx->expr(0));
    string          addr2 = getAddrDecor(ctx->expr(1));
    instructionList code1 = getCodeDecor(ctx->expr(0));
    instructionList code2 = getCodeDecor(ctx->expr(1));
    instructionList code  = code1 || code2;
    // TypesMgr::TypeId t1 = getTypeDecor(ctx->expr(0));
    // TypesMgr::TypeId t2 = getTypeDecor(ctx->expr(1));
    // TypesMgr::TypeId t  = getTypeDecor(ctx);
    string temp = "%"+codeCounters.newTEMP();
    if (ctx->AND()) 
        code = code || instruction::AND(temp, addr1, addr2);
    else 
        code = code || instruction::OR(temp, addr1, addr2);

    putAddrDecor(ctx, temp);
    putOffsetDecor(ctx, "");
    putCodeDecor(ctx, code);
    DEBUG_EXIT();
}

void CodeGenListener::enterParExpr(AslParser::ParExprContext *ctx) {}
void CodeGenListener::exitParExpr(AslParser::ParExprContext *ctx) {
    string          addr = getAddrDecor(ctx->expr());
    instructionList code = getCodeDecor(ctx->expr());

    putOffsetDecor(ctx, "");
    putAddrDecor(ctx,addr);
    putCodeDecor(ctx,code);
}

void CodeGenListener::enterUnaryExpr(AslParser::UnaryExprContext *ctx) {}
void CodeGenListener::exitUnaryExpr(AslParser::UnaryExprContext *ctx) {
    string          addr = getAddrDecor(ctx->expr());
    instructionList code = getCodeDecor(ctx->expr());
    TypesMgr::TypeId t   = getTypeDecor(ctx->expr());

    string temp;
    if (Types.isIntegerTy(t)) {
        if (ctx->MINUS()) {
            temp = "%"+codeCounters.newTEMP();
            code = code || instruction::NEG(temp, addr);
        }
        else if (ctx->PLUS()) {
            temp = addr;
        }
    }
    else if (Types.isFloatTy(t)) {
        if (ctx->MINUS()) {
            temp = "%"+codeCounters.newTEMP();
            code = code || instruction::FNEG(temp, addr);
        }
        else if (ctx->PLUS()) {
            temp = addr;
        }
    }
    else {
        if (ctx->NOT()) {
            temp = "%"+codeCounters.newTEMP();
            code = code || instruction::NOT(temp, addr);
        }
    }

    putOffsetDecor(ctx, "");
    putAddrDecor(ctx,temp);
    putCodeDecor(ctx,code);
}

void CodeGenListener::enterAtomExpr(AslParser::AtomExprContext *ctx) {}
void CodeGenListener::exitAtomExpr(AslParser::AtomExprContext *ctx) {
    putCodeDecor(ctx, getCodeDecor(ctx->atom()));
    putAddrDecor(ctx, getAddrDecor(ctx->atom()));
    putOffsetDecor(ctx, getOffsetDecor(ctx->atom()));
}

//void CodeGenListener::enterParExpr(AslParser::ParExprContext *ctx) {}
//void CodeGenListener::exitParExpr(AslParser::ParExprContext *ctx) {}

void CodeGenListener::enterAtom(AslParser::AtomContext *ctx) {}
void CodeGenListener::exitAtom(AslParser::AtomContext *ctx) {
    if (ctx->boolExpr()) {
        putCodeDecor(ctx, getCodeDecor(ctx->boolExpr()));
        putAddrDecor(ctx, getAddrDecor(ctx->boolExpr()));
        putOffsetDecor(ctx, getOffsetDecor(ctx->boolExpr()));
    }
    else if (ctx->value()) {
        putCodeDecor(ctx, getCodeDecor(ctx->value()));
        putAddrDecor(ctx, getAddrDecor(ctx->value()));
        putOffsetDecor(ctx, getOffsetDecor(ctx->value()));
    }
    else if (ctx->chart()) {
        putCodeDecor(ctx, getCodeDecor(ctx->chart()));
        putAddrDecor(ctx, getAddrDecor(ctx->chart()));
        putOffsetDecor(ctx, getOffsetDecor(ctx->chart()));
    }
    else if (ctx->fval()) {
        putCodeDecor(ctx, getCodeDecor(ctx->fval()));
        putAddrDecor(ctx, getAddrDecor(ctx->fval()));
        putOffsetDecor(ctx, getOffsetDecor(ctx->fval()));    
    }
}

void CodeGenListener::enterValue(AslParser::ValueContext *ctx) { DEBUG_ENTER(); }
void CodeGenListener::exitValue(AslParser::ValueContext *ctx) {
    instructionList code;
    string temp = "%"+codeCounters.newTEMP();
    code = instruction::ILOAD(temp, ctx->getText());
    putAddrDecor(ctx, temp);
    putOffsetDecor(ctx, "");
    putCodeDecor(ctx, code);
    DEBUG_EXIT();
}

void CodeGenListener::enterChart(AslParser::ChartContext *ctx) {}
void CodeGenListener::exitChart(AslParser::ChartContext *ctx) {
    instructionList code;
    string temp = "%"+codeCounters.newTEMP();
    string ch = ctx->CHARVAL()->getText();
    ch.pop_back();
    ch.erase(ch.begin());
    code = instruction::CHLOAD(temp, ch);
    putAddrDecor(ctx, temp);
    putOffsetDecor(ctx, "");
    putCodeDecor(ctx, code);
    DEBUG_EXIT();
}

void CodeGenListener::enterBoolExpr(AslParser::BoolExprContext *ctx) {}
void CodeGenListener::exitBoolExpr(AslParser::BoolExprContext *ctx) {
    instructionList code;
    string temp = "%"+codeCounters.newTEMP();
    string boolVal = "0";
    if (ctx->getText() == "true") boolVal = "1";
    code = instruction::ILOAD(temp, boolVal);
    putAddrDecor(ctx, temp);
    putOffsetDecor(ctx, "");
    putCodeDecor(ctx, code);
    DEBUG_EXIT();
}

void CodeGenListener::enterFval(AslParser::FvalContext *ctx) {}
void CodeGenListener::exitFval(AslParser::FvalContext *ctx) {
    instructionList code;
    string temp = "%"+codeCounters.newTEMP();
    code = instruction::FLOAD(temp, ctx->getText());
    putAddrDecor(ctx, temp);
    putOffsetDecor(ctx, "");
    putCodeDecor(ctx, code);
    DEBUG_EXIT();
}

void CodeGenListener::enterExprIdent(AslParser::ExprIdentContext *ctx) {
  DEBUG_ENTER();
}
void CodeGenListener::exitExprIdent(AslParser::ExprIdentContext *ctx) {
    putAddrDecor(ctx, getAddrDecor(ctx->ident()));
    putOffsetDecor(ctx, getOffsetDecor(ctx->ident()));
    putCodeDecor(ctx, getCodeDecor(ctx->ident()));
    DEBUG_EXIT();
}

void CodeGenListener::enterIdent(AslParser::IdentContext *ctx) {
    DEBUG_ENTER();
}
void CodeGenListener::exitIdent(AslParser::IdentContext *ctx) {
    putAddrDecor(ctx, ctx->ID()->getText());
    putOffsetDecor(ctx, "");
    putCodeDecor(ctx, instructionList());
    DEBUG_EXIT();
}

// void CodeGenListener::enterEveryRule(antlr4::ParserRuleContext *ctx) {
//   DEBUG_ENTER();
// }
// void CodeGenListener::exitEveryRule(antlr4::ParserRuleContext *ctx) {
//   DEBUG_EXIT();
// }
// void CodeGenListener::visitTerminal(antlr4::tree::TerminalNode *node) {
//   DEBUG(">>> visit " << node->getSymbol()->getLine() << ":" << node->getSymbol()->getCharPositionInLine() << " CodeGen TerminalNode");
// }
// void CodeGenListener::visitErrorNode(antlr4::tree::ErrorNode *node) {
// }


// Getters for the necessary tree node atributes:
//   Scope, Type, Addr, Offset and Code
SymTable::ScopeId CodeGenListener::getScopeDecor(antlr4::ParserRuleContext *ctx) {
  return Decorations.getScope(ctx);
}
TypesMgr::TypeId CodeGenListener::getTypeDecor(antlr4::ParserRuleContext *ctx) {
  return Decorations.getType(ctx);
}
string CodeGenListener::getAddrDecor(antlr4::ParserRuleContext *ctx) {
  return Decorations.getAddr(ctx);
}
string  CodeGenListener::getOffsetDecor(antlr4::ParserRuleContext *ctx) {
  return Decorations.getOffset(ctx);
}
instructionList CodeGenListener::getCodeDecor(antlr4::ParserRuleContext *ctx) {
  return Decorations.getCode(ctx);
}

// Setters for the necessary tree node attributes:
//   Addr, Offset and Code
void CodeGenListener::putAddrDecor(antlr4::ParserRuleContext *ctx, const string & a) {
  Decorations.putAddr(ctx, a);
}
void CodeGenListener::putOffsetDecor(antlr4::ParserRuleContext *ctx, const string & o) {
  Decorations.putOffset(ctx, o);
}
void CodeGenListener::putCodeDecor(antlr4::ParserRuleContext *ctx, const instructionList & c) {
  Decorations.putCode(ctx, c);
}
