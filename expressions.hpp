#ifndef NEKO_INTERPRETER_EXPRESSIONS_HPP
#define NEKO_INTERPRETER_EXPRESSIONS_HPP

#include "token.hpp"
#include <variant>
#include "OperationProcessing.hpp"
#include "execution.hpp"
#include "Object.hpp"

struct VariableObject {
  private:
  set<string> type{"Any"};

  public:
  bool isMutable = true;
  string name = "";
  Item item = Item(emptyToken);

  bool isAnyType() {
	  return type.find("Any") != type.end();
  }

  void addType(string t) {
	  if (type.find("Any") != type.end()) {
		  type.clear();
	  }
	  type.insert(t);
  }

  set<string> getType() {
	  return type;
  }

  bool containType(string t) {
	  return type.find(t) != type.end();
  }
};

struct ExecuteReturned {
  Exception exception;
  Item item;

  ExecuteReturned(Item i, Exception e) : item(i), exception(e) {}

  ExecuteReturned(Item i) : item(i), exception(Nothing) {}

  ExecuteReturned(Exception e) : item(""), exception(e) {}
};

ExecuteReturned execute(vector<Token> input);

ExecuteReturned execute(vector<Token> input, vector<VariableObject> init);


struct FunctionReturned {
  Item item;
  Exception exception;
  bool isVoid;

  FunctionReturned(Item i, Exception e, bool v = false) : item(i), exception(e), isVoid(v) {}

  FunctionReturned(Item i) : item(i), exception(Nothing), isVoid(false) {}

  FunctionReturned(Exception e) : item(""), exception(e), isVoid(false) {}
};

FunctionReturned parseFunctionCall(const vector<Token> &input, int &index);

const FunctionReturned VoidResult = {Item(getToken("")), Nothing, true};

FunctionReturned runWithArgs(Function function, vector<Item> init) {
	if (init.size() < function.args.size()) {
		return Exception(FunctionArgumentLack, function.startIndex);
	}

	if (init.size() > function.args.size()) {
		return Exception(FunctionArgumentExcess, function.startIndex);
	}

	for (int i = 0; i < function.args.size(); ++i) {
		if (function.args[i].type == init[i].type or function.args[i].type == "Any") {
			continue;
		}
		return Exception(TypeError);
	}

	vector<VariableObject> variables;

	for (int i = 0; i < function.args.size(); ++i) {
		VariableObject v;
		v.item = init[i];
		v.addType(function.args[i].type);
		v.name = function.args[i].name;
		variables.push_back(v);
	}

	// TODO: add args
	ExecuteReturned result = execute(function.representation, variables);
	if (result.exception.type == RETURN) {
		if (not function.containType(result.item.type) and not function.isAnyType()) {
			return Exception(TypeError);
		}
		return result.item;
	}
	if (result.exception.type != Nothing) {
		return Exception(result.exception.type, result.exception.line + function.startIndex);
	}
	if (not function.containType("Unit") and not function.isAnyType()) {
		return Exception(EndOfFunction);
	}
	return Exception(Nothing);
}

struct ScopeManager {
  public:
  vector<map<string, VariableObject>> m;

  void addScope() {
	  m.push_back(map<string, VariableObject>());
  }

  Exception add(VariableObject v) {
	  if (m.back().find(v.name) != m.back().end()) {
		  return RedefinitionError;
	  }
	  m.back()[v.name] = v;
	  return Nothing;
  }

  VariableObject get(string name) {
	  for (int i = m.size() - 1; i >= 0; --i) {
		  if (m[i].find(name) != m[i].end()) {
			  return m[i][name];
		  }
	  }
	  assert(false);
  }

  void setItem(string name, Item item) {
	  for (int i = m.size() - 1; i >= 0; --i) {
		  if (m[i].find(name) != m[i].end()) {
			  m[i][name].item = item;
			  return;
		  }
	  }
	  assert(false);
  }

  Function getFunction(string name) {
	  VariableObject obj = get(name);

	  return *static_cast<Function *>(obj.item.value);
  }

  bool find(string name) {
	  for (int i = m.size() - 1; i >= 0; --i) {
		  if (m[i].find(name) != m[i].end()) {
			  return true;
		  }
	  }
	  return false;
  }

  void deleteLastScope() {
	  m.pop_back();
  }
};

ScopeManager scopeManager;

ParseExpressionReturned parseExpression(const vector<Token> &input, int &index);

bool isLeftAssociative(Item item) {
	assert(toType(item.type) == OperationType);
	return item.source != "**";
}

vector<Item> intoPostfixNotation(vector<Item> input) {
	stack<Token> operations;
	vector<Item> output;
	int end = input.size();
	for (int i = 0; i < end; ++i) {
		Token token = input[i].token;
		if (token.source == "(") {
			operations.push(token);
			continue;
		}
		if (token.isOperator()) {
			while (not operations.empty()) {
				if (operations.top().source == "(") {
					break;
				}
				if (isLeftAssociative(token) and getPriority(operations.top()) < getPriority(token)) {
					break;
				}
				if (not isLeftAssociative(token) and getPriority(operations.top()) <= getPriority(token)) {
					break;
				}
				output.push_back(operations.top());
				operations.pop();
			}
			operations.push(token);
		} else {
			if (token.source == ")") {
				while (operations.top().source != "(") {
					output.push_back(operations.top());
					operations.pop();
				}
				operations.pop();
			} else {
				output.push_back(input[i]);
			}
		}
	}
	while (not operations.empty()) {
		output.push_back(operations.top());
		operations.pop();
	}
	return output;
}

enum nameType {
  DeclaredVariable,
  DeclaredFunction,
  DeclaredClass,
  Undeclared
};

nameType nameDeclaration(string name) {
	if (BuiltInFunctions.find(name) != BuiltInFunctions.end()) {
		return DeclaredFunction;
	}
	if (scopeManager.find(name)) {
		VariableObject obj = scopeManager.get(name);
		if (obj.containType("Function") or obj.item.type == "Function") {
			return DeclaredFunction;
		}
		return DeclaredVariable;
	}
	if (Classes.find(name) != Classes.end() or BuiltInClasses.find(name) != BuiltInClasses.end()) {
		return DeclaredClass;
	}
	return Undeclared;
}

struct ProcessReturned {
  Item item;
  Exception exception;

  ProcessReturned(Item i, Exception e = Nothing) : item(i), exception(e) {}

  ProcessReturned(Exception e) : item(""), exception(e) {}
};

bool isNumber(Item item) {
	return contain({toString(IntType), toString(FloatType)}, item.type);
}

bool possibleToProcess(Item &a, Item &b, Token op) {
	if (a.type == b.type) {
		return true;
	}
	if (isNumber(a) and isNumber(b)) {
		return true;
	}
	if (op.source == "*" and
	    ((a.type == "Int" and b.type == "String") or (a.type == "String" and b.type == "Int"))) {
		return true;
	}
	if (op.source == "+" and
	    ((a.type == "Char" and b.type == "String") or (a.type == "String" and b.type == "Char"))) {
		return true;
	}
}

// применение оператора к двум операндам
ProcessReturned Process(Item a, Item b, Token op) {
	if (a.type != b.type and not(isNumber(a) and isNumber(b)) and not possibleToProcess(a, b, op)) {
		return Exception(TypeError);
	}
	// умножение строки на число
	if (op.source == "*") {
		if (a.type == "String" and b.type == "Int") {
			string s = static_cast<String *>(a.value)->value;
			long long x = static_cast<Int *>(b.value)->value;
			return Item(static_cast<void *>(new String(multiply(s, x))), "String");
		}
		if (a.type == "Int" and b.type == "String") {
			string s = static_cast<String *>(b.value)->value;
			long long x = static_cast<Int *>(a.value)->value;
			return Item(static_cast<void *>(new String(multiply(s, x))), "String");
		}
	}
	// сложение String и Char
	if (op.source == "+") {
		if (a.type == "String" and b.type == "Char") {
			string s = static_cast<String *>(a.value)->value;
			char x = static_cast<Char *>(b.value)->value;
			return Item(static_cast<void *>(new String(x + s)), "String");
		}
		if (a.type == "Char" and b.type == "String") {
			string s = static_cast<String *>(b.value)->value;
			char x = static_cast<Char *>(a.value)->value;
			return Item(static_cast<void *>(new String(s + x)), "String");
		}
	}
	if (not contain(possibleOperations(toType(a.type)), op.source)) {
		return Exception(OperandTypeError);
	}
	if (a.type == "Int" and b.type == "Int") {
		long long x = static_cast<Int *>(a.value)->value, y = static_cast<Int *>(b.value)->value;
		if (op.source == "/" and x == 0) {
			return Exception(ZeroDivisionError);
		}
		if (isComparisonOperation(op.source)) {
			return Item(static_cast<void *>(new Bool(compare(x, y, op.source))), "Bool");
		}
		return Item(static_cast<void *>(new Int(processOperation(x, y, op.source))), "Int");
	}
	if (a.type == "Float" or b.type == "Float") {
		long double x = (a.type == "Int" ? static_cast<Int *>(a.value)->value : static_cast<Float *>(a.value)->value),
			y = (b.type == "Int" ? static_cast<Int *>(b.value)->value : static_cast<Float *>(b.value)->value);
		if (op.source == "/" and x == 0) {
			return Exception(ZeroDivisionError);
		}
		if (isComparisonOperation(op.source)) {
			return Item(static_cast<void *>(new Bool(compare(x, y, op.source))), "Bool");
		}
		return Item(static_cast<void *>(new Float(processOperation(x, y, op.source))), "Float");
	}
	if (a.type == "Bool") {
		bool x = static_cast<Bool *>(a.value)->value, y = static_cast<Bool *>(b.value)->value;
		if (isComparisonOperation(op.source)) {
			return Item(static_cast<void *>(new Bool(compare(x, y, op.source))), "Bool");
		}
		return Item(static_cast<void *>(new Bool(processOperation(x, y, op.source))), "Bool");
	}
	if (a.type == "String") {
		string x = static_cast<String *>(a.value)->value, y = static_cast<String *>(b.value)->value;
		if (isComparisonOperation(op.source)) {
			return Item(static_cast<void *>(new Bool(compare(x, y, op.source))), "Bool");
		}
		return Item(static_cast<void *>(new String(processOperation(x, y, op.source))), "String");
	}
	if (a.type == "Char") {
		char x = static_cast<Char *>(a.value)->value, y = static_cast<Char *>(b.value)->value;
		if (isComparisonOperation(op.source)) {
			return Item(static_cast<void *>(new Bool(compare(x, y, op.source))), "Bool");
		}
		return Item(static_cast<void *>(new Char(processOperation(x, y, op.source))), "Char");
	}
}

ProcessReturned ProcessUnary(Item &a, Token op) {
	if (not contain({"Int", "Float", "Bool"}, a.type)) {
		return Exception(IncorrectOperationArguments);
	}
	if (not contain({"$+", "$-", "!"}, op.source)) {
		return Exception(OperationArgumentExcess);
	}
	if (a.type == "Int") {
		long long x = static_cast<Int *>(a.value)->value;
		if (op.source == "$-") x = -x;
		if (op.source == "!") x = !x;
		return Item(static_cast<void *>(new Int(x)), "Int");
	}
	if (a.type == "Float") {
		long double x = static_cast<Float *>(a.value)->value;
		if (op.source == "$-") x = -x;
		if (op.source == "!") x = !x;
		return Item(static_cast<void *>(new Float(x)), "Float");
	}
	if (a.type == "Bool") {
		bool x = static_cast<Bool *>(a.value)->value;
		if (op.source != "!") {
			return Exception(IncorrectOperationArguments);
		}
		return Item(static_cast<void *>(new Bool(!x)), "Bool");
	}
}

CalculateReturned Calculate(Expression expression) {
//	if (expression.content.size() == 1) {
//		Item ret = expression.content[0];
//		if (ret.token.type == Name) {
//			if (scopeManager.find(ret.token.source)) {
//				return Exception(UndefinedNameUsage);
//			}
//			return scopeManager.get(ret.token.source).item;
//		}
//		return ret;
//	}

	vector<Item> postfixNotation = intoPostfixNotation(expression.content);
	stack<Item> values;
	// вычисление выражений
	for (auto elem: postfixNotation) {
		if (elem.type == "Operation") {
			if (values.empty()) {
				return Exception(RuntimeError);
			}
			Item a = values.top();
			values.pop();
			if (values.empty() or elem.token.isUnaryOperator()) {
				auto processedUnary = ProcessUnary(a, elem.token);
				if (processedUnary.exception.type != Nothing) {
					return processedUnary.exception;
				}
				values.push(processedUnary.item);
				continue;
			}
			Item b = values.top();
			values.pop();
			auto processed = Process(a, b, elem.token);
			if (processed.exception.type != Nothing) {
				return processed.exception;
			}
			values.push(processed.item);
		} else {
			values.push(elem);
		}
	}
	if (values.size() != 1) {
		return Exception(RuntimeError);
	}
	return values.top();
}


struct VariableAssignmentReturned {
  Item item;
  Exception exception;

  VariableAssignmentReturned(Item i, Exception e) : item(i), exception(e) {}

  VariableAssignmentReturned(Item i) : item(i), exception(Nothing) {}

  VariableAssignmentReturned(Exception e) : item(""), exception(e) {}
};

VariableAssignmentReturned parseVariableAssignment(const vector<Token> &input, int &index);

struct FunctionDeclarationParsed {
  Function function;
  Exception exception;

  FunctionDeclarationParsed(Function f, Exception e) : function(f), exception(e) {}

  FunctionDeclarationParsed(Function f) : function(f), exception(Nothing) {}

  FunctionDeclarationParsed(Exception e) : function(Function()), exception(e) {}
};

FunctionDeclarationParsed parseFunctionDeclaration(const vector<Token> &input, int &index);

struct TernaryReturned {
  Item item;
  Exception exception;

  TernaryReturned(Item i, Exception e) : item(i), exception(e) {}

  TernaryReturned(Item i) : item(i), exception(Nothing) {}

  TernaryReturned(Exception e) : item(Item("")), exception(e) {}
};

TernaryReturned parseTernary(const vector<Token> &input, int &index);

// TODO: add method call
ParseExpressionReturned parseExpression(const vector<Token> &input, int &index) {
	Token token = input[index];
	int end = input.size() - 1;
	Expression ret;
	stack<Token> bracketStack;
	int firstIndex = index;
	while (index < end) {
		token = input[index];
		Token prevToken = prev(input, index), nextToken = next(input, index);
		// условия выхода из expression-а
		if (token.type == EOE or (token.isRightBracket() and bracketStack.empty()) or token.source == ",") {
			if (index != firstIndex) {
				// index = nextIndex(input, index);
				break;
			}
		}
		if (index != 0) {
			if ((prevToken.isObject() or prevToken.isRightBracket() or prevToken.isKeyword()) and
			    (token.isObject() or token.isLeftBracket() or token.isKeyword() or token.isUnaryOperator())) {
				if (index != firstIndex) {
					if (contain({EOL, EOE}, input[index - 1].type) or token.source == "else") {
						// index = nextIndex(input, index);
						break;
					} else {
						return Exception(SyntaxError, getLineIndex(input, index));
					}
				}
			}
		}
		if (token.isBracket()) {
			if (bracketStack.empty()) {
				bracketStack.push(token);
			} else {
				if (isBracketPair(bracketStack.top(), token)) {
					bracketStack.pop();
				} else {
					bracketStack.push(token);
				}
			}
		}

		// parse call TODO: change to isCallable()
		if (token.isObject() and input[index + 1].source == "(") {
			if (token.type == Name and nameDeclaration(token.source) == DeclaredFunction) {
				FunctionReturned functionReturned = parseFunctionCall(input, index);
				if (functionReturned.exception.type != Nothing) {
					return functionReturned.exception;
				}
				ret.content.push_back(functionReturned.item);
			} else if (token.type == Name and nameDeclaration(token.source) == DeclaredClass) {
				ConstructorReturned constructorReturned = parseConstructorCall(input, index);
				if (constructorReturned.exception.type != Nothing) {
					return constructorReturned.exception;
				}
				ret.content.push_back(constructorReturned.item);
				continue;
			} else {
				return Exception(CallError, getLineIndex(input, index));
			}
			continue;
		}

		// parse indexation
		if (token.isObject() and nextToken.source == "[") {

			continue;
		}

		// parse array declaration
		if (token.source == "[") {
			// auto result = parseArray(input, index);
			continue;
		}

		// parse ternary operator
		if (token.source == "if") {
			auto result = parseTernary(input, index);
			if (result.exception.type != Nothing) {
				return result.exception;
			}
			ret.content.push_back(result.item);
			continue;
		}

		// parse lambda
		if (token.source == "lambda") {
			auto result = parseFunctionDeclaration(input, index);
			if (result.exception.type != Nothing) {
				return result.exception;
			}
			ret.content.push_back(Item(static_cast<void *>(new Function(result.function)), "Function"));
			continue;
		}

		// обращение к полю или методу
		if (token.isObject() and input[index + 1].source == ".") {
			continue;
		}

		if (token.type == Name) {
			if (nameDeclaration(token.source) == Undeclared) {
				return Exception(UndefinedNameUsage, getLineIndex(input, index));
			}
			if (nameDeclaration(token.source) != Undeclared and nextToken.source != ".") {
				if (nextToken.source == "=") {
					auto result = parseVariableAssignment(input, index);
					if (result.exception.type != Nothing) {
						return Exception(result.exception.type, getLineIndex(input, index));
					}
					ret.content.push_back(result.item);
					continue;
				}
				ret.content.push_back(scopeManager.get(token.source).item);
				index = nextIndex(input, index);
				continue;
			}
//			if (nameDeclaration(token.source) == DeclaredFunction) {
//				FunctionReturned functionReturned = parseFunctionCall(input, index);
//				if (functionReturned.exception.type != Nothing) {
//					return functionReturned.exception;
//				}
//				ret.content.push_back(functionReturned.item);
//				continue;
//			}
		}
		// cout << "[" << token.source << "]" << endl;
		// TODO: возможны ошибки
		if (token.type == Keyword and not contain({"this"}, token.source)) {
			return Exception(SyntaxError, getLineIndex(input, index));
		}
		ret.content.push_back(Item(token));
		index = nextIndex(input, index);
	}
//	if (not contain({EOE, EOL}, input[index - 1].type) and not contain({EOE, EOL}, token.type) and
//	    not token.isRightBracket() and token.source != ",") {
//		return Exception(SyntaxError);
//	}
	return ret;
}

#endif //NEKO_INTERPRETER_EXPRESSIONS_HPP