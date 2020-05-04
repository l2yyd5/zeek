// See the file "COPYING" in the main distribution directory for copyright.

#pragma once

// Zeek statements.

#include "StmtBase.h"

#include "BroList.h"
#include "Dict.h"
#include "ID.h"


class ExprListStmt : public Stmt {
public:
	const ListExpr* ExprList() const	{ return l.get(); }

	TraversalCode Traverse(TraversalCallback* cb) const override;

protected:
	ExprListStmt(BroStmtTag t, IntrusivePtr<ListExpr> arg_l);

	~ExprListStmt() override;

	IntrusivePtr<Val> Exec(Frame* f, stmt_flow_type& flow) const override;
	virtual IntrusivePtr<Val> DoExec(const std::vector<IntrusivePtr<Val>>& vals,
	                                 stmt_flow_type& flow) const = 0;

	bool IsReduced() const override;
	Stmt* DoReduce(Reducer* c) override;

	// Returns a new version of the original derived object
	// based on the given list of singleton expressions.
	virtual Stmt* DoSubclassReduce(IntrusivePtr<ListExpr> singletons,
				Reducer* c) = 0;

	void StmtDescribe(ODesc* d) const override;

	IntrusivePtr<ListExpr> l;
};

class PrintStmt : public ExprListStmt {
public:
	template<typename L>
	explicit PrintStmt(L&& l) : ExprListStmt(STMT_PRINT, std::forward<L>(l)) { }

protected:
	IntrusivePtr<Val> DoExec(const std::vector<IntrusivePtr<Val>>& vals,
	                         stmt_flow_type& flow) const override;

	Stmt* DoSubclassReduce(IntrusivePtr<ListExpr> singletons,
			Reducer* c) override;

	const CompiledStmt Compile(Compiler* c) const override;
};

extern void do_print(const std::vector<IntrusivePtr<Val>>& vals);


class ExprStmt : public Stmt {
public:
	explicit ExprStmt(IntrusivePtr<Expr> e);
	~ExprStmt() override;

	IntrusivePtr<Val> Exec(Frame* f, stmt_flow_type& flow) const override;

	const Expr* StmtExpr() const	{ return e.get(); }

	void StmtDescribe(ODesc* d) const override;

	TraversalCode Traverse(TraversalCallback* cb) const override;

protected:
	ExprStmt(BroStmtTag t, IntrusivePtr<Expr> e);

	virtual IntrusivePtr<Val> DoExec(Frame* f, Val* v, stmt_flow_type& flow) const;

	bool IsPure() const override;
	bool IsReduced() const override;
	Stmt* DoReduce(Reducer* c) override;

	const CompiledStmt Compile(Compiler* c) const override;

	IntrusivePtr<Expr> e;
};

class IfStmt : public ExprStmt {
public:
	IfStmt(IntrusivePtr<Expr> test, IntrusivePtr<Stmt> s1, IntrusivePtr<Stmt> s2);
	~IfStmt() override;

	const Stmt* TrueBranch() const	{ return s1.get(); }
	const Stmt* FalseBranch() const	{ return s2.get(); }

	void StmtDescribe(ODesc* d) const override;

	TraversalCode Traverse(TraversalCallback* cb) const override;

protected:
	IntrusivePtr<Val> DoExec(Frame* f, Val* v, stmt_flow_type& flow) const override;
	bool IsPure() const override;
	bool IsReduced() const override;
	Stmt* DoReduce(Reducer* c) override;

	const CompiledStmt Compile(Compiler* c) const override;

	IntrusivePtr<Stmt> s1;
	IntrusivePtr<Stmt> s2;
};

class Case : public BroObj {
public:
	Case(IntrusivePtr<ListExpr> c, id_list* types, IntrusivePtr<Stmt> arg_s);
	~Case() override;

	const ListExpr* ExprCases() const	{ return expr_cases.get(); }
	ListExpr* ExprCases()		{ return expr_cases.get(); }

	const id_list* TypeCases() const	{ return type_cases; }
	id_list* TypeCases()		{ return type_cases; }

	const Stmt* Body() const	{ return s.get(); }
	Stmt* Body()			{ return s.get(); }

	void UpdateBody(Stmt* new_body)	{ s = {AdoptRef{}, new_body}; }

	void Describe(ODesc* d) const override;

	TraversalCode Traverse(TraversalCallback* cb) const;

protected:
	IntrusivePtr<ListExpr> expr_cases;
	id_list* type_cases;
	IntrusivePtr<Stmt> s;
};

typedef PList<Case> case_list;

class SwitchStmt : public ExprStmt {
public:
	SwitchStmt(IntrusivePtr<Expr> index, case_list* cases);
	~SwitchStmt() override;

	const case_list* Cases() const	{ return cases; }
	bool HasDefault() const		{ return default_case_idx != -1; }

	void StmtDescribe(ODesc* d) const override;

	TraversalCode Traverse(TraversalCallback* cb) const override;

	// For use by the compiler; could make protected and add a "friend",
	// but it's a bit ugly to have to add the subclass name since
	// "friend Compiler" won't be inherited.
	int DefaultCaseIndex() const	{ return default_case_idx; }
	const PDict<int>* ValueMap() const
		{ return &case_label_value_map; }
	const std::vector<std::pair<ID*, int>>* TypeMap() const
		{ return &case_label_type_list; }
	const CompositeHash* CompHash() const	{ return comp_hash; }

protected:
	IntrusivePtr<Val> DoExec(Frame* f, Val* v, stmt_flow_type& flow) const override;
	bool IsPure() const override;
	bool IsReduced() const override;
	Stmt* DoReduce(Reducer* c) override;

	const CompiledStmt Compile(Compiler* c) const override;

	// Initialize composite hash and case label map.
	void Init();

	// Adds an entry in case_label_value_map for the given value to
	// associate it with the given index in the cases list.  If the
	// entry already exists, returns false, else returns true.
	bool AddCaseLabelValueMapping(const Val* v, int idx);

	// Adds an entry in case_label_type_map for the given type (w/ ID) to
	// associate it with the given index in the cases list.  If an entry
	// for the type already exists, returns false; else returns true.
	bool AddCaseLabelTypeMapping(ID* t, int idx);

	// Returns index of a case label that matches the value, or
	// default_case_idx if no case label matches (which may be -1 if
	// there's no default label). The second tuple element is the ID of
	// the matching type-based case if it defines one.
	std::pair<int, ID*> FindCaseLabelMatch(const Val* v) const;

	case_list* cases;
	int default_case_idx;
	CompositeHash* comp_hash;
	PDict<int> case_label_value_map;
	std::vector<std::pair<ID*, int>> case_label_type_list;
};

class AddDelStmt : public ExprStmt {
public:
	bool IsPure() const override;

	Stmt* DoReduce(Reducer* c) override;
	bool IsReduced() const override;

	TraversalCode Traverse(TraversalCallback* cb) const override;

protected:
	AddDelStmt(BroStmtTag t, IntrusivePtr<Expr> arg_e);
};

class AddStmt : public AddDelStmt {
public:
	explicit AddStmt(IntrusivePtr<Expr> e);

	IntrusivePtr<Val> Exec(Frame* f, stmt_flow_type& flow) const override;

	const CompiledStmt Compile(Compiler* c) const override;
};

class DelStmt : public AddDelStmt {
public:
	explicit DelStmt(IntrusivePtr<Expr> e);

	IntrusivePtr<Val> Exec(Frame* f, stmt_flow_type& flow) const override;

	const CompiledStmt Compile(Compiler* c) const override;
};

class EventStmt : public ExprStmt {
public:
	explicit EventStmt(IntrusivePtr<EventExpr> e);

	IntrusivePtr<Val> Exec(Frame* f, stmt_flow_type& flow) const override;

	Stmt* DoReduce(Reducer* c) override;

	const CompiledStmt Compile(Compiler* c) const override;

	TraversalCode Traverse(TraversalCallback* cb) const override;

protected:
	IntrusivePtr<EventExpr> event_expr;
};

class WhileStmt : public Stmt {
public:

	WhileStmt(IntrusivePtr<Expr> loop_condition, IntrusivePtr<Stmt> body);
	~WhileStmt() override;

	bool IsPure() const override;
	bool IsReduced() const override;
	Stmt* DoReduce(Reducer* c) override;

	const Expr* Condition() const	{ return loop_condition.get(); }

	// If we construct a loop_cond_stmt, then for optimization
	// it turns out to be helpful if we have a *statement* associated
	// with evaluating the conditional as well as an expression,
	// so we construct one in that case.
	const Stmt* ConditionAsStmt() const
		{ return stmt_loop_condition.get(); }

	const Stmt* CondStmt() const
		{ return loop_cond_stmt ? loop_cond_stmt.get() : nullptr; }
	const Stmt* Body() const	{ return body.get(); }

	void StmtDescribe(ODesc* d) const override;

	TraversalCode Traverse(TraversalCallback* cb) const override;

protected:
	IntrusivePtr<Val> Exec(Frame* f, stmt_flow_type& flow) const override;

	const CompiledStmt Compile(Compiler* c) const override;

	IntrusivePtr<Expr> loop_condition;
	IntrusivePtr<Stmt> stmt_loop_condition;
	IntrusivePtr<Stmt> loop_cond_stmt;
	IntrusivePtr<Stmt> body;
};

class ForStmt : public ExprStmt {
public:
	ForStmt(id_list* loop_vars, IntrusivePtr<Expr> loop_expr);
	// Special constructor for key value for loop.
	ForStmt(id_list* loop_vars, IntrusivePtr<Expr> loop_expr, IntrusivePtr<ID> val_var);
	~ForStmt() override;

	void AddBody(IntrusivePtr<Stmt> arg_body)	{ body = std::move(arg_body); }

	id_list* LoopVars() const	{ return loop_vars; }
	ID* ValueVar() const		{ return value_var.get(); }
	const Expr* LoopExpr() const	{ return e.get(); }
	const Stmt* LoopBody() const	{ return body.get(); }

	bool IsPure() const override;
	bool IsReduced() const override;
	Stmt* DoReduce(Reducer* c) override;

	void StmtDescribe(ODesc* d) const override;

	TraversalCode Traverse(TraversalCallback* cb) const override;

protected:
	IntrusivePtr<Val> DoExec(Frame* f, Val* v, stmt_flow_type& flow) const override;

	const CompiledStmt Compile(Compiler* c) const override;

	id_list* loop_vars;
	IntrusivePtr<Stmt> body;
	// Stores the value variable being used for a key value for loop.
	// Always set to nullptr unless special constructor is called.
	IntrusivePtr<ID> value_var;
};

class NextStmt : public Stmt {
public:
	NextStmt() : Stmt(STMT_NEXT)	{ }

	IntrusivePtr<Val> Exec(Frame* f, stmt_flow_type& flow) const override;

	const CompiledStmt Compile(Compiler* c) const override;

	bool IsPure() const override;

	void StmtDescribe(ODesc* d) const override;

	TraversalCode Traverse(TraversalCallback* cb) const override;

protected:
};

class BreakStmt : public Stmt {
public:
	BreakStmt() : Stmt(STMT_BREAK)	{ }

	IntrusivePtr<Val> Exec(Frame* f, stmt_flow_type& flow) const override;

	const CompiledStmt Compile(Compiler* c) const override;

	bool IsPure() const override;

	void StmtDescribe(ODesc* d) const override;

	TraversalCode Traverse(TraversalCallback* cb) const override;

protected:
};

class FallthroughStmt : public Stmt {
public:
	FallthroughStmt() : Stmt(STMT_FALLTHROUGH)	{ }

	IntrusivePtr<Val> Exec(Frame* f, stmt_flow_type& flow) const override;

	const CompiledStmt Compile(Compiler* c) const override;

	bool IsPure() const override;

	void StmtDescribe(ODesc* d) const override;

	TraversalCode Traverse(TraversalCallback* cb) const override;

protected:
};

class ReturnStmt : public ExprStmt {
public:
	explicit ReturnStmt(IntrusivePtr<Expr> e);

	IntrusivePtr<Val> Exec(Frame* f, stmt_flow_type& flow) const override;

	Stmt* DoReduce(Reducer* c) override;
	const CompiledStmt Compile(Compiler* c) const override;

	void StmtDescribe(ODesc* d) const override;
};

class StmtList : public Stmt {
public:
	StmtList();

	// Idioms commonly used in reduction.
	StmtList(IntrusivePtr<Stmt> s1, Stmt* s2);
	StmtList(IntrusivePtr<Stmt> s1, IntrusivePtr<Stmt> s2);
	StmtList(IntrusivePtr<Stmt> s1, IntrusivePtr<Stmt> s2,
			IntrusivePtr<Stmt> s3);

	~StmtList() override;

	IntrusivePtr<Val> Exec(Frame* f, stmt_flow_type& flow) const override;

	Stmt* DoReduce(Reducer* c) override;
	const CompiledStmt Compile(Compiler* c) const override;

	const stmt_list& Stmts() const	{ return *stmts; }
	stmt_list& Stmts()		{ return *stmts; }

	void StmtDescribe(ODesc* d) const override;

	TraversalCode Traverse(TraversalCallback* cb) const override;

protected:
	bool ReduceStmt(int& s_i, stmt_list* f_stmts, Reducer* c);

	void ResetStmts(stmt_list* new_stmts)
		{
		delete stmts;
		stmts = new_stmts;
		}

	bool IsPure() const override;
	bool IsReduced() const override;

	stmt_list* stmts;
};

class InitStmt : public Stmt {
public:
	explicit InitStmt(id_list* arg_inits);

	~InitStmt() override;

	IntrusivePtr<Val> Exec(Frame* f, stmt_flow_type& flow) const override;

	const CompiledStmt Compile(Compiler* c) const override;

	const id_list* Inits() const	{ return inits; }

	void StmtDescribe(ODesc* d) const override;

	TraversalCode Traverse(TraversalCallback* cb) const override;

protected:
	id_list* inits;
};

class NullStmt : public Stmt {
public:
	NullStmt() : Stmt(STMT_NULL)	{ }

	IntrusivePtr<Val> Exec(Frame* f, stmt_flow_type& flow) const override;

	const CompiledStmt Compile(Compiler* c) const override;

	bool IsPure() const override;

	void StmtDescribe(ODesc* d) const override;

	TraversalCode Traverse(TraversalCallback* cb) const override;
};

class WhenStmt : public Stmt {
public:
	// s2 is null if no timeout block given.
	WhenStmt(IntrusivePtr<Expr> cond,
	         IntrusivePtr<Stmt> s1, IntrusivePtr<Stmt> s2,
	         IntrusivePtr<Expr> timeout, bool is_return);
	~WhenStmt() override;

	IntrusivePtr<Val> Exec(Frame* f, stmt_flow_type& flow) const override;

	const CompiledStmt Compile(Compiler* c) const override;

	bool IsPure() const override;
	bool IsReduced() const override;

	const Expr* Cond() const	{ return cond.get(); }
	const Stmt* Body() const	{ return s1.get(); }
	const Expr* TimeoutExpr() const	{ return timeout.get(); }
	const Stmt* TimeoutBody() const	{ return s2.get(); }

	void StmtDescribe(ODesc* d) const override;

	TraversalCode Traverse(TraversalCallback* cb) const override;

protected:
	IntrusivePtr<Expr> cond;
	IntrusivePtr<Stmt> s1;
	IntrusivePtr<Stmt> s2;
	IntrusivePtr<Expr> timeout;
	bool is_return;
};

class CheckAnyLenStmt : public ExprStmt {
public:
	explicit CheckAnyLenStmt(IntrusivePtr<Expr> e, int expected_len);

	IntrusivePtr<Val> Exec(Frame* f, stmt_flow_type& flow) const override;

	const CompiledStmt Compile(Compiler* c) const override;

	bool IsReduced() const override;
	Stmt* DoReduce(Reducer* c) override;

	void StmtDescribe(ODesc* d) const override;

protected:
	int expected_len;
};
