# EngineScript Grammar and Semantic Examples (Milestone R1)

## 1. EBNF Grammar Specification

```ebnf
Module              ::= ( ImportDecl | Declaration )* EOF ;

ImportDecl          ::= "import" ( "{" Ident ( "," Ident )* "}" | "*" "as" Ident | Ident ) "from" StringLiteral ";" ;

Declaration         ::= DecoratorList? ( ClassDecl | StructDecl | InterfaceDecl | EnumDecl | FunctionDecl | ConstDecl ) ;

DecoratorList       ::= Decorator+ ;
Decorator           ::= "@" Ident ( "(" ( Expr ( "," Expr )* | ObjectLiteral )? ")" )? ;

ClassDecl           ::= "export"? "class" Ident ( "<" TypeParamList ">" )? ( "extends" TypeRef )? ( "implements" TypeRef ( "," TypeRef )* )? "{" ClassMember* "}" ;
StructDecl          ::= "export"? "struct" Ident ( "<" TypeParamList ">" )? "{" StructMember* "}" ;
InterfaceDecl       ::= "export"? "interface" Ident ( "<" TypeParamList ">" )? ( "extends" TypeRef ( "," TypeRef )* )? "{" InterfaceMember* "}" ;
EnumDecl            ::= "export"? "enum" Ident "{" ( Ident ( "=" ( IntegerLiteral | StringLiteral ) )? ( "," Ident ( "=" ( IntegerLiteral | StringLiteral ) )? )* )? ","? "}" ;
FunctionDecl        ::= "export"? "async"? "function" Ident ( "<" TypeParamList ">" )? "(" ParamList? ")" ( ":" TypeRef )? Block ;
ConstDecl           ::= "export"? "const" Ident ( ":" TypeRef )? "=" Expr ";" ;

ClassMember         ::= DecoratorList? ( ConstructorDecl | MethodDecl | PropertyDecl ) ;
ConstructorDecl     ::= "constructor" "(" ConstructorParamList? ")" Block ;
ConstructorParam    ::= DecoratorList? ( "private" | "protected" | "public" )? Ident ":" TypeRef ( "=" Expr )? ;
ConstructorParamList::= ConstructorParam ( "," ConstructorParam )* ;

MethodDecl          ::= "override"? "async"? Ident ( "<" TypeParamList ">" )? "(" ParamList? ")" ( ":" TypeRef )? Block ;
PropertyDecl        ::= Ident ( ":" TypeRef )? ( "=" Expr )? ";" ;

StructMember        ::= PropertyDecl | MethodDecl ;
InterfaceMember     ::= Ident ( "<" TypeParamList ">" )? "(" ParamList? ")" ":" TypeRef ";"
                      | Ident ":" TypeRef ";" ;

ParamList           ::= Param ( "," Param )* ;
Param               ::= Ident ":" TypeRef ( "=" Expr )? ;

TypeParamList       ::= TypeParam ( "," TypeParam )* ;
TypeParam           ::= Ident ( "extends" TypeRef )? ;

TypeRef             ::= PrimitiveType | Ident ( "<" TypeRef ( "," TypeRef )* ">" )? ( "?" )? ;
PrimitiveType       ::= "Void" | "Bool" | "Int8" | "Int16" | "Int32" | "Int64" | "UInt8" | "UInt16" | "UInt32" | "UInt64"
                      | "Int" | "UInt" | "Float" | "Float32" | "Float64" | "String" | "Vec2" | "Vec3" | "Vec4" | "Color"
                      | "Duration" | "Entity" ;

Statement           ::= VarDeclStmt
                      | ReturnStmt
                      | IfStmt
                      | WhileStmt
                      | ForStmt
                      | ForInStmt
                      | BreakStmt
                      | ContinueStmt
                      | ExprStmt
                      | Block ;

VarDeclStmt         ::= ( "let" | "const" ) Ident ( ":" TypeRef )? "=" Expr ";" ;
ReturnStmt          ::= "return" Expr? ";" ;
IfStmt              ::= "if" "(" Expr ")" Statement ( "else" Statement )? ;
WhileStmt           ::= "while" "(" Expr ")" Statement ;
ForStmt             ::= "for" "(" ( VarDeclStmt | ExprStmt | ";" ) Expr? ";" Expr? ")" Statement ;
ForInStmt           ::= "for" "(" "let" Ident "in" Expr ")" Statement ;
BreakStmt           ::= "break" ";" ;
ContinueStmt        ::= "continue" ";" ;
ExprStmt            ::= Expr ";" ;
Block               ::= "{" Statement* "}" ;

Expr                ::= Assignment ;
Assignment          ::= LogicalOr ( ( "=" | "+=" | "-=" | "*=" | "/=" ) Assignment )? ;
LogicalOr           ::= LogicalAnd ( "||" LogicalAnd )* ;
LogicalAnd          ::= Equality ( "&&" Equality )* ;
Equality            ::= Relational ( ( "==" | "!=" ) Relational )* ;
Relational          ::= NullCoalescing ( ( "<" | "<=" | ">" | ">=" ) NullCoalescing )* ;
NullCoalescing      ::= Additive ( "??" Additive )* ;
Additive            ::= Multiplicative ( ( "+" | "-" ) Multiplicative )* ;
Multiplicative      ::= Unary ( ( "*" | "/" | "%" ) Unary )* ;
Unary               ::= ( "!" | "-" | "+" | "await" ) Unary | Postfix ;
Postfix             ::= Primary ( "(" ArgList? ")" | "." Ident | "?." Ident | "[" Expr "]" )* ;

Primary             ::= Literal
                      | Ident
                      | "(" Expr ")"
                      | ArrayLiteral
                      | ObjectLiteral
                      | ArrowFunction ;

ArrayLiteral        ::= "[" ( Expr ( "," Expr )* )? ","? "]" ;
ObjectLiteral       ::= "{" ( ObjectEntry ( "," ObjectEntry )* )? ","? "}" ;
ObjectEntry         ::= Ident ":" Expr ;
ArrowFunction       ::= "(" ParamList? ")" "=>" ( Expr | Block ) ;
ArgList             ::= Expr ( "," Expr )* ;

Literal             ::= IntegerLiteral | FloatLiteral | DurationLiteral | StringLiteral | BooleanLiteral | "null" ;
DurationLiteral     ::= ( IntegerLiteral | FloatLiteral ) "." ( "ms" | "seconds" | "s" | "minutes" | "min" ) ;
BooleanLiteral      ::= "true" | "false" ;
```

---

## 2. Verified Semantic Examples

### 2.1 Platformer Player (`Behaviours/PlatformerPlayer.e2s`)
```typescript
@behaviour
export class PlatformerPlayer extends Behaviour {
    constructor(
        @inject private input: Input,
        @component private body: RigidBody2D,
        @optional private animator: Animator2D?
    ) {}

    @expose({ min: 0.0, max: 1000.0 })
    walkSpeed: Float = 220.0;

    @expose
    jumpSpeed: Float = 480.0;

    override update(deltaTime: Float): Void {
        let axis: Float = input.axis("Horizontal");
        body.velocity.x = axis * walkSpeed;
        if (input.pressed("Jump") && body.isGrounded) {
            body.velocity.y = -jumpSpeed;
        }
    }

    async respawn(): Task<Void> {
        await delay(500.ms);
        animator?.play("idle");
    }
}
```

### 2.2 Inventory Item Value Types (`Data/Inventory.e2s`)
```typescript
export struct ItemSlot {
    itemId: Int = 0;
    count: Int = 1;
    durability: Float = 100.0;
}

export class Inventory {
    private slots: Array<ItemSlot> = [];

    addItem(slot: ItemSlot): Bool {
        if (this.slots.length >= 20) {
            return false;
        }
        this.slots.push(slot);
        return true;
    }

    findSlot(itemId: Int): ItemSlot? {
        for (let i = 0; i < this.slots.length; i += 1) {
            if (this.slots[i].itemId == itemId) {
                return this.slots[i];
            }
        }
        return null;
    }
}
```

### 2.3 Damage & Result Handling (`Systems/Combat.e2s`)
```typescript
export struct DamageEvent {
    amount: Float;
    instigator: Entity;
}

export function applyDamage(targetHp: Float, damage: Float): Result<Float, String> {
    if (damage < 0.0) {
        return Result.err("Damage cannot be negative");
    }
    let newHp: Float = targetHp - damage;
    if (newHp < 0.0) {
        newHp = 0.0;
    }
    return Result.ok(newHp);
}
```
