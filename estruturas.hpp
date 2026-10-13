// =============================================================================
//  Estruturas de dados do jogo (implementadas à mão, sem std::list/queue/stack)
//
//    Lista<T> ... lista duplamente encadeada  -> a corrente de bolas no trilho
//    Fila<T> .... fila (FIFO) encadeada       -> a munição do sapo
//    Pilha<T> ... pilha (LIFO) encadeada      -> reação em cadeia das bombas
// =============================================================================
#pragma once

#include <cstddef>

// ----------------------------------------------------------------------------
//  Lista duplamente encadeada
//
//  Cada nó guarda o valor e os ponteiros para o vizinho de trás (anterior) e o
//  da frente (proximo). Inserir e remover um nó, dado o ponteiro, é O(1).
//  Os ponteiros para nós continuam válidos enquanto o nó não for removido.
// ----------------------------------------------------------------------------
template <typename T>
class Lista {
public:
    struct No {
        T valor;
        No* anterior;
        No* proximo;
    };

    Lista() = default;
    Lista(const Lista&) = delete;
    Lista& operator=(const Lista&) = delete;
    ~Lista() { limpar(); }

    bool vazia() const { return tam == 0; }
    std::size_t tamanho() const { return tam; }
    No* primeiro() const { return inicio; }
    No* ultimo() const { return fim; }

    No* inserirInicio(const T& valor) {
        No* no = new No{valor, nullptr, inicio};
        if (inicio) inicio->anterior = no;
        else fim = no;
        inicio = no;
        ++tam;
        return no;
    }

    No* inserirFim(const T& valor) {
        No* no = new No{valor, fim, nullptr};
        if (fim) fim->proximo = no;
        else inicio = no;
        fim = no;
        ++tam;
        return no;
    }

    // Insere logo antes de `ref` (que deve pertencer à lista).
    No* inserirAntes(No* ref, const T& valor) {
        if (ref == inicio) return inserirInicio(valor);
        No* no = new No{valor, ref->anterior, ref};
        ref->anterior->proximo = no;
        ref->anterior = no;
        ++tam;
        return no;
    }

    // Insere logo depois de `ref` (que deve pertencer à lista).
    No* inserirDepois(No* ref, const T& valor) {
        if (ref == fim) return inserirFim(valor);
        No* no = new No{valor, ref, ref->proximo};
        ref->proximo->anterior = no;
        ref->proximo = no;
        ++tam;
        return no;
    }

    // Desliga o nó dos vizinhos e libera a memória. Depois disso `no` é inválido.
    void remover(No* no) {
        if (no->anterior) no->anterior->proximo = no->proximo;
        else inicio = no->proximo;
        if (no->proximo) no->proximo->anterior = no->anterior;
        else fim = no->anterior;
        delete no;
        --tam;
    }

    void removerFim() { remover(fim); }

    void limpar() {
        while (inicio) remover(inicio);
    }

    // Permite `for (T& v : lista)`, percorrendo do início para o fim.
    class Iterador {
    public:
        explicit Iterador(No* no) : atual(no) {}
        T& operator*() const { return atual->valor; }
        Iterador& operator++() {
            atual = atual->proximo;
            return *this;
        }
        bool operator!=(const Iterador& o) const { return atual != o.atual; }

    private:
        No* atual;
    };
    Iterador begin() const { return Iterador(inicio); }
    Iterador end() const { return Iterador(nullptr); }

private:
    No* inicio = nullptr;
    No* fim = nullptr;
    std::size_t tam = 0;
};

// ----------------------------------------------------------------------------
//  Fila (FIFO) encadeada: entra pelo fim, sai pela frente.
// ----------------------------------------------------------------------------
template <typename T>
class Fila {
public:
    Fila() = default;
    Fila(const Fila&) = delete;
    Fila& operator=(const Fila&) = delete;
    ~Fila() { limpar(); }

    bool vazia() const { return tam == 0; }
    std::size_t tamanho() const { return tam; }

    void enfileirar(const T& valor) {
        No* no = new No{valor, nullptr};
        if (fim) fim->proximo = no;
        else inicio = no;
        fim = no;
        ++tam;
    }

    T desenfileirar() {
        No* no = inicio;
        T valor = no->valor;
        inicio = no->proximo;
        if (!inicio) fim = nullptr;
        delete no;
        --tam;
        return valor;
    }

    T& frente() { return inicio->valor; }

    // i-ésimo elemento a partir da frente (0 = o próximo a sair).
    T& em(std::size_t i) {
        No* no = inicio;
        while (i-- > 0) no = no->proximo;
        return no->valor;
    }

    void limpar() {
        while (!vazia()) desenfileirar();
    }

private:
    struct No {
        T valor;
        No* proximo;
    };
    No* inicio = nullptr;
    No* fim = nullptr;
    std::size_t tam = 0;
};

// ----------------------------------------------------------------------------
//  Pilha (LIFO) encadeada: entra e sai pelo topo.
// ----------------------------------------------------------------------------
template <typename T>
class Pilha {
public:
    Pilha() = default;
    Pilha(const Pilha&) = delete;
    Pilha& operator=(const Pilha&) = delete;
    ~Pilha() { limpar(); }

    bool vazia() const { return tam == 0; }
    std::size_t tamanho() const { return tam; }

    void empilhar(const T& valor) {
        topo = new No{valor, topo};
        ++tam;
    }

    T desempilhar() {
        No* no = topo;
        T valor = no->valor;
        topo = no->abaixo;
        delete no;
        --tam;
        return valor;
    }

    T& olhar() { return topo->valor; }

    void limpar() {
        while (!vazia()) desempilhar();
    }

private:
    struct No {
        T valor;
        No* abaixo;
    };
    No* topo = nullptr;
    std::size_t tam = 0;
};
