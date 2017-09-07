#ifndef HIT_PARSE
#define HIT_PARSE

#include <string>
#include <vector>
#include <algorithm>
#include <typeinfo>

#include "lex.h"

/// The hit namespace provides functions and objects used for interpreting and manipulating
/// hit formatted inputs.  The hit language syntax is defined by the following context free
/// grammar (CFG):
///
///     section => section_header section_body section_terminator
///     section_header => LEFT_BRACKET PATH RIGHT_BRACKET
///     section_terminator => LEFT_BRACKET CLOSING_PATH RIGHT_BRACKET
///     section_body => section_entry section_body | section_entry
///     section_entry => parameter | section
///     parameter => PATH EQUALS param_value
///     param_value => string | NUMBER | BOOL
///     string => UNQUOTED_STRING_BODY
///             | SINGLE_QUOTE SINGLE_QUOTE_BODY SINGLE_QUOTE
///             | DOUBLE_QUOTE DOUBLE_QUOTE_BODY DOUBLE_QUOTE
///
/// Where the terminals are defined as:
///
///     LEFT_BRACKET = "["
///     RIGHT_BRACKET = "]"
///     EQUALS = "="
///     NUMBER = [+-]?[0-9]*(\.[0-9]*)?([eE][+-][0-9]+)?
///     PATH = [a-zA-Z0-9_./:<>+\-]+
///     CLOSING_PATH = "../" | ""
///     BOOL = TRUE|true|YES|yes|ON|on|FALSE|false|NO|no|OFF|off
///     UNQUOTED_STRING_BODY = [^ \t\n\[]+
///     SINGLE_QUOTE = "'"
///     DOUBLE_QUOTE = "\""
///     SINGLE_QUOTE_BODY = ([^\\']|\\')*
///     DOUBLE_QUOTE_BODY = ([^\\"]|\\")*
///
/// Intervening whitespace between the terminals is ignored.
///
/// The starting point function for using this module/API is the parse function which parses a
/// hit input and returns a corresponding syntax tree of nodes.  Then you can use the returned
/// root-node to do useful things like retrieving parameter values from the hit file, etc:
///
///     std::string myinput = "[hello] world=42 []";
///     std::unique_ptr<hit::Node> root(hit::parse(myinput));
///     std::cout << root->param<int>("hello/world") << "\n"; // prints "42"
///
/// See documentation comments for the Node class for details about lots of useful functionality.
namespace hit
{

/// NodeType represents every element type in a parsed hit tree.
enum class NodeType
{
  All,     /// Used for tree-traversal/manipulation to indicate all functions.
  Root,    /// Represents the root, most un-nested node of a parsed hit tree.
  Section, /// Represents hit sections (i.e. "[pathname]...[../]").
  Comment, /// Represents comments that are not directly part of the actual hit document.
  Field,   /// Represents field-value pairs (i.e. paramname=val).
};

/// nodeTypeName returns a human-readable string representing a name for the give node type.
std::string nodeTypeName(NodeType t);

class Node;

/// Error is the superclass for all hit parser related errors.  This includes errors for
/// requesting values of the wrong type from a parsed hit tree.
class Error : public std::exception
{
public:
  Error(const std::string & msg);
  virtual const char * what() const throw() override;
  std::string msg;
};

/// ParserError represents a parsing error (i.e. bad syntax, invalid characters, etc.).
class ParseError : public Error
{
public:
  ParseError(const std::string & msg);
};

/// Walker is an abstract interface that can be implemented to perform operations that traverse ag
/// parsed hit node tree.  Implementing classes are passed to the Node::walk function.
class Walker
{
public:
  /// walk is called when the walker is passed into a Node::walk function for each relevant node in
  /// the hit (sub)tree.  fullpath is the fully-qualified (absolute) path to the hit node
  /// where each section header is a path-element.  nodepath is the path for the node of interest -
  /// section name for Section nodes and field/parameter name for Field nodes.  n is the actual
  /// node.
  virtual void walk(const std::string & fullpath, const std::string & nodepath, Node * n) = 0;
};

/// strRepeat returns a string of s repeated n times.
std::string strRepeat(const std::string & s, int n);

/// pathNorm returns the canonical, normalized version of the given hit path.  It removes
/// consecutive slashes and leading './' among other things.
std::string pathNorm(const std::string & path);

/// pathJoin a joined version of the given hit (relative) paths as single hit path.
std::string pathJoin(const std::vector<std::string> & paths);

/// Node represents an object in a parsed hit tree.
class Node
{
public:
  Node(NodeType t);
  virtual ~Node();

  /// type returns the type of the node (e.g. one of Field, Section, Comment, etc.)
  NodeType type();
  /// path returns this node's local/direct contribution its full hit path.  For section nodes, this
  /// is the section name, for field nodes, this is the field/parameter name, for other nodes this
  /// is empty.
  virtual std::string path();
  /// fullpath returns the full hit path to this node (including all parent sections
  /// recursively) starting from the tree's root node.
  std::string fullpath();
  /// tokens returns all raw lexer tokens that this node was generated from.  This can be useful
  /// for determining locations in the original file of different tree nodes/elements.
  std::vector<Token> & tokens();
  /// line returns the line number of the original parsed input (file) that contained the start of
  /// the content that this node was built from.
  int line();

  /// the following functions return the stored value of the node (if any exists) of the type
  /// indicated in the function name. If the node holds a value of a different type or doesn't hold
  /// a value at all, an exception will be thrown.
  virtual bool boolVal();
  virtual int intVal();
  virtual double floatVal();
  /// strVal is special in that it only throws an exception if the node doesn't hold a value at
  /// all.  All nodes with a value hold data that was originally represented as a string in the
  /// parsed input - so this returns that raw string.
  virtual std::string strVal();
  /// the vec-prefixed value retrieval functions assume the node holds a string-typed value holding
  /// whitespace delimited entries of the element type indicated in the function name.
  virtual std::vector<double> vecFloatVal();
  virtual std::vector<int> vecIntVal();
  virtual std::vector<std::string> vecStrVal();

  /// addChild adds a node to the ordered set of this node's children.  This node assumes/takes
  /// ownership of the memory of the passed child.
  void addChild(Node * child);
  /// children returns a list of this node's children of the given type t.
  std::vector<Node *> children(NodeType t = NodeType::All);
  /// parent returns a pointer to this node's parent node or nullptr if this node has no parent.
  Node * parent();
  /// root returns the root node for the gepot tree this node resides in.
  Node * root();
  /// clone returns a complete (deep) copy of this node.  The caller will be responsible for
  /// managing the memory/deallocation of the returned clone node.
  virtual Node * clone() = 0;

  /// render builds an hit syntax/text that is equivalent to the hit tree starting at this
  /// node (and downward) - i.e. parsing this function's returned string would yield a node tree
  /// identical to this nodes tree downward.
  virtual std::string render(int indent = 0);

  /// walk does a depth-first traversal of the hit tree starting at this node (it
  /// doesn't visit any nodes that require traversing this node's parent) calling the passed
  /// walker's walk function for each node visited.  w->walk is not called for nodes that are not
  /// of type t although nodes not of type t are still traversed.
  void walk(Walker * w, NodeType t = NodeType::Field);

  /// find follows the tree along the given path starting at this node (downward not checking any
  /// nodes that require traversing this node's parent) and returns the first node it finds at the
  /// given relative path if any or nullptr otherwise.
  Node * find(const std::string & path);

  /// param searches for the node at the given path (empty path indicates *this* node) and returns
  /// the value stored at that node in the form of the given type T.  The node at the given path
  /// must hold a value (i.e. be a Field node) otherwise an exception is thrown.  If the node holds
  /// a value that cannot be represented as type T, an exception is also thrown.  All (field) nodes
  /// can return their value as a std::string type.
  template <typename T>
  T param(const std::string & path = "")
  {
    auto n = this;
    if (path != "")
      n = find(path);
    if (n == nullptr)
      throw Error("no parameter named '" + path + "'");
    return paramInner<T>(n);
  }

  /// paramOptional is identical to param except if no node is found at the given path, default_val
  /// is returned instead of the value returned by the param function.
  template <typename T>
  T paramOptional(const std::string & path, T default_val)
  {
    if (find(path) == nullptr)
      return default_val;
    return param<T>(path);
  }

protected:
  NodeType _type;

private:
  Node * findInner(const std::string & path, const std::string & prefix);

  template <typename T>
  T paramInner(Node *)
  {
    throw Error("unsupported c++ type '" + std::string(typeid(T).name()) + "'");
  }

  std::vector<Token> _toks;
  Node * _parent = nullptr;
  std::vector<Node *> _children;
};

template <>
inline bool
Node::paramInner(Node * n)
{
  return n->boolVal();
}
template <>
inline int
Node::paramInner(Node * n)
{
  return n->intVal();
}
template <>
inline unsigned int
Node::paramInner(Node * n)
{
  return n->intVal();
}
template <>
inline float
Node::paramInner(Node * n)
{
  return n->floatVal();
}
template <>
inline double
Node::paramInner(Node * n)
{
  return n->floatVal();
}
template <>
inline std::string
Node::paramInner(Node * n)
{
  return n->strVal();
}
template <>
inline std::vector<int>
Node::paramInner(Node * n)
{
  return n->vecIntVal();
}
template <>
inline std::vector<unsigned int>
Node::paramInner(Node * n)
{
  auto tmp = n->vecIntVal();
  std::vector<unsigned int> vec;
  for (auto val : tmp)
    vec.push_back(val);
  return vec;
}
template <>
inline std::vector<double>
Node::paramInner(Node * n)
{
  return n->vecFloatVal();
}
template <>
inline std::vector<float>
Node::paramInner(Node * n)
{
  auto tmp = n->vecFloatVal();
  std::vector<float> vec;
  for (auto val : tmp)
    vec.push_back(val);
  return vec;
}
template <>
inline std::vector<std::string>
Node::paramInner(Node * n)
{
  return n->vecStrVal();
}

/// Comment repsents an in-file comment (i.e. "# some comment text...")
class Comment : public Node
{
public:
  static const bool Inline = true;
  static const bool Block = false;
  Comment(const std::string & text, bool is_inline);

  virtual std::string render(int indent = 0) override;
  virtual Node * clone() override;

private:
  std::string _text;
  bool _isinline;
};

/// Section represents a hit section including the section header path and all entries inside
/// the section (e.g. fields/parameters, subsections, etc.).
class Section : public Node
{
public:
  Section(const std::string & path);

  /// path returns the hit path located in the section's header i.e. the section's name.
  virtual std::string path() override;

  virtual std::string render(int indent = 0) override;
  virtual Node * clone() override;

private:
  std::string _path;
};

/// Field represents the field-value pairs or parameters that provide the meat content of hit
/// documents.  Fields have a name, a kind (e.g. Int, Bool, Float, String), and a value.

class Field : public Node
{
public:
  // Kind represents all possible value types that can be stored in a field.
  enum class Kind
  {
    None,
    Int,
    Float,
    Bool,
    String,
  };
  Field(const std::string & field, Kind k, const std::string & val);

  /// path returns the hit Field name (i.e. content before the "=")
  virtual std::string path() override;

  virtual std::string render(int indent = 0) override;
  virtual Node * clone() override;

  /// kind returns the semantic type of the value stored in this field (e.g. Int, Bool, Float,
  /// String).
  Kind kind();

  /// setVal is a way to overwrite and set the field's underlying value content to anything.  This
  /// does not affect the value returned by the kind function by default (e.g. setting a "Bool"
  /// field to "42" does not make the field's kind "Int" - it will just cause errors when you try
  /// to retrieve the value via anything other than strVal).
  void setVal(const std::string & val, Kind kind = Kind::None);
  /// val returns the raw text of the field's value as it was read from the hit input.  This is
  /// the value set by setVal.
  std::string val();

  virtual std::vector<double> vecFloatVal() override;
  virtual std::vector<int> vecIntVal() override;
  virtual std::vector<std::string> vecStrVal() override;
  virtual bool boolVal() override;
  virtual int intVal() override;
  virtual double floatVal() override;
  virtual std::string strVal() override;

private:
  Kind _kind;
  std::string _path;
  std::string _field;
  std::string _val;
};

/// parse is *the* function in the hit namespace.  It takes the given hit input text and
/// parses and builds a hit tree returning the root node.  It throws an exceptions if input
/// contains any invalid hit syntax.  fname is label given as a convenience (and can be any
/// string) used to prefix any error messages generated during the parsing process.  The caller
/// accepts ownership of the returned root node and is responsible for destructing it.
Node * parse(const std::string & fname, const std::string & input);

/// parses the file checking for errors, but don't return any built node tree.
inline void
check(const std::string & fname, const std::string & input)
{
  delete parse(fname, input);
}

/// Merges the hit tree under from into the hit tree under into.  If a node (identified by
/// its full path) is present in both trees, the one in from overwrites the one in into.  Nodes in
/// from but not present in into are cloned.  The from tree remains unmodified.  Line numbers for
/// cloned nodes remain the same as they were in the original from tree - referring to the input
/// that was parsed to create the from tree.  In general you should only merge already-exploded
/// node trees.
void merge(Node * from, Node * into);

/// explode walks the tree converting/exploding any fields that have path separators into them into
/// actually sections/subsections/etc. with the final path element as the field name.  For example,
/// "foo/bar=42" becomes nodes with the structure "[foo] bar=42 []".  If nodes for sections already
/// exist in the tree, the fields will be moved into them rather than new sections created.
void explode(Node * n);

} // namespace hit

#endif