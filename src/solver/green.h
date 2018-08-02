#include "../base_system.h"
#include "hamiltonian.h"

#include <cstdio>
#include <unordered_map>
#include <vector>
#include "../util.h"

template <class S>
class Green {
 public:
  Green(S& system, Hamiltonian<S>& hamiltonian) : system(system), hamiltonian(hamiltonian) {}

  void run();

 private:
  size_t n_dets;

  size_t n_pdets;

  unsigned n_orbs;

  double w;

  double n;

  std::vector<Det> dets_store;

  std::vector<double> coefs_store;

  std::unordered_map<Det, size_t, DetHasher> pdet_to_id;

  S& system;

  Hamiltonian<S>& hamiltonian;

  std::vector<std::vector<std::complex<double>>> G;

  void construct_pdets();

  std::vector<double> construct_b(const unsigned orb);

  void green_ham();

  std::vector<std::complex<double>> cg(const SparseMatrix& matrix, const std::vector<double>& b, const std::vector<std::complex<double>>& x0); 

  void output_green();
};

template <class S>
void Green<S>::run() {
  // Store dets and coefs.
  dets_store = system.dets;
  coefs_store = system.coefs;
  n_dets = dets_store.size();
  n_orbs = system.n_orbs;

  // Construct new dets.
  system.dets.clear();
  system.coefs.clear();
  construct_pdets();

  // Construct hamiltonian.
  hamiltonian.clear();
  hamiltonian.update(system);
  green_ham();

  // Initialize G.
  G.resize(n_pdets);
  for (unsigned i = 0; i < n_orbs * 2; i++) {
    G[i].assign(n_pdets, 0.0);
  }

  for (unsigned j = 0; j < n_orbs * 2; j++) {
    Timer::checkpoint(Util::str_printf("orb #%zu/%zu", j + 1, n_orbs * 2));
    // Construct bj
    const auto& bj = construct_b(j);

    // Generate initial x.

    std::vector<std::complex<double>> x0(n_pdets, sqrt(1.0 / n_pdets));
    //
    // Iteratively get H^{-1}bj
    const auto& x = cg(hamiltonian.matrix, bj, x0);

    for (unsigned i = 0; i < n_orbs * 2; i++) {
      // Dot with bi
      const auto& bi = construct_b(i);
      G[i][j] = Util::dot_omp(bi, x);
    }
  }

  output_green();
}

template <class S>
void Green<S>::construct_pdets() {
  printf("n_dets: %zu\n", n_dets);
  printf("n_orbs: %u\n", n_orbs);
  for (size_t i = 0; i < n_dets; i++) {
    Det det = dets_store[i];
    for (unsigned k = 0; k < n_orbs; k++) {
      if (!det.up.has(k)) {
        det.up.set(k);
        if (pdet_to_id.count(det) == 0) {
          pdet_to_id[det] = system.dets.size();
          system.dets.push_back(det);
        }
        det.up.unset(k);
      }
      if (!det.dn.has(k)) {
        det.dn.set(k);
        if (pdet_to_id.count(det) == 0) {
          pdet_to_id[det] = system.dets.size();
          system.dets.push_back(det);
        }
        det.dn.unset(k);
      }
    }
  }
  n_pdets = system.dets.size();
  system.coefs.assign(n_pdets, 0.0);
}

template <class S>
std::vector<double> Green<S>::construct_b(const unsigned j) {
  std::vector<double> b(n_pdets, 0.0);
#pragma omp parallel for schedule(static, 1)
  for (size_t det_id = 0; det_id < n_dets; det_id++) {
    Det det = dets_store[det_id];
    if (j < n_orbs && !det.up.has(j)) {
      det.up.set(j);
    } else if (j >= n_orbs && !det.dn.has(j - n_orbs)) {
      det.dn.set(j - n_orbs);
    } else {
      continue;
    }
    const size_t pdet_id = pdet_to_id[det];
    b[pdet_id] = coefs_store[det_id];
  }
  return b;
}

template <class S>
void Green<S>::green_ham() {
  w = Config::get<double>("w_green", 1.0);
  n = Config::get<double>("n_green", 1.0);
  const double energy_var = system.energy_var;
  const std::complex<double> offset(w + energy_var, n);
  hamiltonian.matrix.set_green(offset);
}

template <class S>
void Green<S>::output_green() {
  const auto& filename = Util::str_printf("green_%#.2e_%#.2ei.csv", w, n);
  FILE* file = fopen(filename.c_str(), "w");
  fprintf(file, "i,j,G\n");
  for (unsigned i = 0; i < n_orbs * 2; i++) {
    for (unsigned j = 0; j < n_orbs * 2; j++) {
      fprintf(file, "%u,%u,%g%+gj\n", i, j, G[i][j].real(), G[i][j].imag());
    }
  }
  fclose(file);
  printf("Green's function saved to: %s\n", filename.c_str());
}

template <class S>
std::vector<std::complex<double>> Green<S>::cg(const SparseMatrix& matrix, const std::vector<double>& b, const std::vector<std::complex<double>>& x0) {
  std::vector<std::complex<double>> x(n_pdets, 0.0);
  std::vector<std::complex<double>> r(n_pdets, 0.0);
  std::vector<std::complex<double>> p(n_pdets, 0.0);

  const auto& Ax0 = matrix.mul_green(x0);
#pragma omp parallel for
  for (size_t i = 0; i < n_pdets; i++) {
    r[i] = b[i] - Ax0[i];
  }
  p = r;
  x = x0;

  double residual = 1.0;
  int iter = 0;
  while (residual > 1.0e-15) {
    const std::complex<double>& rTr = Util::dot_omp(r, r);
    const auto& Ap = matrix.mul_green(p);
    const std::complex<double>& pTAp = Util::dot_omp(p, Ap);
    const std::complex<double>& a = rTr / pTAp;
#pragma omp parallel for
    for (size_t j = 0; j < n_pdets; j++) {
      x[j] += a * p[j];
      r[j] -= a * Ap[j];
    }
    const std::complex<double>& rTr_new = Util::dot_omp(r, r);
    const std::complex<double>& beta = rTr_new / rTr;
#pragma omp parallel for
    for (size_t j = 0; j < n_pdets; j++) {
      p[j] = r[j] + beta * p[j];
    }
    residual = std::abs(rTr);
    iter++;
    if (iter % 10 == 0) printf("Iteration %d: r = %g\n", iter, residual);
    if (iter > 100) throw std::runtime_error("cg does not converge");
  }
  printf("Final iteration %d: r = %g\n", iter, residual);

  return x;
} 
