#include "common.h"
#include "logging.h"
#include "parsing.h"
#include "seq.h"
#include "sequences.h"
#include "local_tree.h"
#include "model.h"

// TODO: add sites validation
//       - positions should be sorted and unique
//       - bases should be acceptable characters


namespace argweaver {


//=============================================================================
// input/output: FASTA

bool read_fasta(FILE *infile, Sequences *seqs)
{
    // store lines until they are ready to discard
    class Discard : public vector<char*> {
    public:
        ~Discard() { clean(); }
        void clean() {
            for (unsigned int i=0; i<size(); i++)
                delete [] at(i);
            clear();
        }
    };


    // init sequences
    seqs->clear();
    seqs->set_owned(true);

    char *line;
    string key;
    vector<char*> seq;
    Discard discard;

    while ((line = fgetline(infile))) {
        chomp(line);

        if (line[0] == '>') {
            // parse key line

            if (seq.size() > 0) {
                // add new sequence
                char *full_seq = concat_strs(&seq[0], seq.size());
                int seqlen2 = strlen(full_seq);
                if (!seqs->append(key, full_seq, seqlen2)) {
                    printError("sequences are not the same length: %d != %d",
                               seqs->length(), seqlen2);
                    delete [] full_seq;
                    return false;
                }
                seq.clear();
                discard.clean();
            }

            // new key found
            key = string(&line[1]);
            delete [] line;
        } else {
            // parse sequence line

            seq.push_back(trim(line));
            discard.push_back(line);
        }
    }

    // add last sequence
    if (seq.size() > 0) {
        char *full_seq = concat_strs(&seq[0], seq.size());
        int seqlen2 = strlen(full_seq);
        if (!seqs->append(key, full_seq, seqlen2)) {
            printError("sequences are not the same length: %d != %d",
                       seqs->length(), seqlen2);
            delete [] full_seq;
            return false;
        }
        discard.clean();
    }

    return true;
}


bool read_fasta(const char *filename, Sequences *seqs)
{
    FILE *infile = NULL;
    if ((infile = fopen(filename, "r")) == NULL) {
        printError("cannot read file '%s'", filename);
        return false;
    }

    bool result = read_fasta(infile, seqs);
    fclose(infile);

    return result;
}



bool write_fasta(const char *filename, Sequences *seqs)
{
    FILE *stream = NULL;

    if ((stream = fopen(filename, "w")) == NULL) {
        fprintf(stderr, "cannot open '%s'\n", filename);
        return false;
    }

    write_fasta(stream, seqs);

    fclose(stream);
    return true;
}

void write_fasta(FILE *stream, Sequences *seqs)
{
    for (int i=0; i<seqs->get_num_seqs(); i++) {
        fprintf(stream, ">%s\n", seqs->names[i].c_str());
        fprintf(stream, "%s\n", seqs->seqs[i]);
    }
}



//=============================================================================
// input/output: sites file format

void write_sites(FILE *stream, Sites *sites) {
  fprintf(stream, "NAMES");
  for (unsigned int i=0; i < sites->names.size(); i++)
    fprintf(stream, "\t%s", sites->names[i].c_str());
  fprintf(stream, "\n");
  fprintf(stream, "REGION\t%s\t%i\t%i\n",
	  sites->chrom.c_str(), sites->start_coord + 1, sites->end_coord);
  if (sites->positions.size() != sites->cols.size()) {
    fprintf(stderr, "Error in write_sites: positions.size()=%i cols.size=%i\n",
	    (int)sites->positions.size(), (int)sites->cols.size());
    exit(-1);
  }
  for (unsigned int i=0; i < sites->positions.size(); i++) {
    unsigned int j=0;
    for (j=1; j < sites->names.size(); j++)
      if (sites->cols[i][j] != sites->cols[i][0]) break;
    if (j != sites->names.size()) {
      fprintf(stream, "%i\t", sites->positions[i]+1);
      for (unsigned int k=0; k < sites->names.size(); k++)
	fprintf(stream, "%c", sites->cols[i][k]);
      fprintf(stream, "%c", '\n');
    }
  }
}

bool validate_site_column(char *col, int nseqs)
{
    for (int i=0; i<nseqs; i++) {
        col[i] = toupper(col[i]);
        if (col[i] != 'N' && dna2int[(int) col[i]] == -1)
            return false;
    }
    return true;
}


// Read a Sites stream
bool read_sites(FILE *infile, Sites *sites,
                int subregion_start, int subregion_end)
{
    const char *delim = "\t";
    char *line;
    int nseqs = 0;

    sites->clear();
    bool error = false;

    // parse lines
    int lineno = 0;
    while (!error && (line = fgetline(infile))) {
        chomp(line);
        lineno++;

        if (strncmp(line, "NAMES\t", 6) == 0) {
            // parse NAMES line
            split(&line[6], delim, sites->names);
            nseqs = sites->names.size();

            // assert every name is non-zero in length
            for (int i=0; i<nseqs; i++) {
                if (sites->names[i].length() == 0) {
                    printError(
                        "name for sequence %d is zero length (line %d)",
                        i + 1, lineno);
                    delete [] line;
                    return false;
                }
            }

        } else if (strncmp(line, "REGION\t", 7) == 0) {
            // parse RANGE line
            char chrom[51];
            if (sscanf(line, "REGION\t%50s\t%d\t%d",
                       chrom,
                       &sites->start_coord, &sites->end_coord) != 3) {
                printError("bad REGION format");
                delete [] line;
                return false;
            }
            sites->chrom = chrom;
            sites->start_coord--;  // convert to 0-index

            // set region by subregion if specified
            if (subregion_start != -1)
                sites->start_coord = subregion_start;
            if (subregion_end != -1)
                sites->end_coord = subregion_end;


        } else if (strncmp(line, "RANGE\t", 6) == 0) {
            // parse RANGE line

            printError("deprecated RANGE line detected (use REGION instead)");
            delete [] line;
            return false;
        } else if (strncmp(line, "POPS\t", 5) == 0) {
            if (nseqs == 0) {
                printError("NAMES line should come before POP line");
                delete [] line;
                return false;
            }
            vector<string> popstr;
            split(&line[5], delim, popstr);
            if ((int)popstr.size() != nseqs) {
                printError("number of entries in POPS line should match entries in NAMES line");
                delete [] line;
                return false;
            }
            for (int i=0; i < nseqs; i++)
                sites->pops.push_back(atoi(popstr[i].c_str()));

        } else {
            // parse a site line

            // parse site
            int position;
            if (sscanf(line, "%d\t", &position) != 1) {
                printError("first column is not an integer (line %d)", lineno);
                delete [] line;
                return false;
            }

            // skip site if not in region
            position--; //convert to 0-index
            if (position < sites->start_coord || position >= sites->end_coord) {
                delete [] line;
                continue;
            }

            // skip first word
            int i=0;
            for (; line[i] != '\t'; i++) {}
            i++;
            char *col = &line[i];

            // parse bases
            unsigned int len = strlen(col);
            if (len != (unsigned int) nseqs) {
                printError(
                    "the number bases given, %d, does not match the "
                    "number of sequences %d (line %d)",
                    len, nseqs, lineno);
                delete [] line;
                return false;
            }
            if (!validate_site_column(col, nseqs)) {
                printError("invalid sequence characters (line %d)", lineno);
                printError("%s\n", line);
                delete [] line;
                return false;
            }

            // validate site locations are unique and sorted.
            int npos = sites->get_num_sites();
            if (npos > 0 && sites->positions[npos-1] >= position) {
                printError("invalid site location %d >= %d (line %d)",
                           sites->positions[npos-1], position, lineno);
                printError("sites must be sorted and unique.");
                delete [] line;
                return false;
            }

            // record site.
            sites->append(position, col, true);
        }

        delete [] line;
    }

    return true;
}


// Read a Sites alignment file
bool read_sites(const char *filename, Sites *sites,
                int subregion_start, int subregion_end)
{
    FILE *infile;
    if ((infile = fopen(filename, "r")) == NULL) {
        printError("cannot read file '%s'", filename);
        return false;
    }

    bool result = read_sites(infile, sites, subregion_start, subregion_end);

    fclose(infile);

    return result;
}


// Converts a Sites alignment to a Sequences alignment
void make_sequences_from_sites(const Sites *sites, Sequences *sequences,
                               char default_char)
{
    int nseqs = sites->names.size();
    int seqlen = sites->length();
    int start = sites->start_coord;
    int nsites = sites->get_num_sites();
    bool have_pops = ( sites->pops.size() > 0);

    sequences->clear();
    sequences->set_owned(true);

    for (int i=0; i<nseqs; i++) {
        char *seq = new char [seqlen];

        int col = 0;
        for (int j=0; j<seqlen; j++) {
            if (col < nsites && start+j == sites->positions[col]) {
                // variant site
                seq[j] = sites->cols[col++][i];
            } else {
                seq[j] = default_char;
            }
        }

        sequences->append(sites->names[i], seq, -1, have_pops ? sites->pops[i] : 0);
    }

    sequences->set_length(seqlen);
}

int Sites::subset(set<string> names_to_keep) {
    vector<int> keep;
    for (unsigned int i=0; i < names.size(); i++) {
        if (names_to_keep.find(names[i]) != names_to_keep.end())
            keep.push_back(i);
    }
    if (keep.size() != names_to_keep.size()) {
        fprintf(stderr, "Error in subset: not all names found in sites\n");
        return 1;
    }
    std::sort(keep.begin(), keep.end());
    vector<string> new_names;
    for (unsigned int i=0; i < keep.size(); i++)
        new_names.push_back(names[keep[i]]);
    names = new_names;
    if (pops.size() != 0) {
        vector<int> new_pops;
        for (unsigned int i=0; i < keep.size(); i++)
            new_pops.push_back(pops[keep[i]]);
        pops = new_pops;
    }
    vector<int> new_positions;
    vector<char*> new_cols;
    for (unsigned int i=0; i < positions.size(); i++) {
        char *tmp = new char[keep.size()+1];
        bool variant=false;
        for (unsigned int j=0; j < keep.size(); j++) {
            tmp[j] = cols[i][keep[j]];
            if (tmp[j]=='N' || tmp[j] != tmp[0]) variant=true;
        }
        tmp[keep.size()] = '\0';
        delete [] cols[i];
        if (variant) {
            new_cols.push_back(tmp);
            new_positions.push_back(positions[i]);
        } else delete [] tmp;
    }
    cols = new_cols;
    positions = new_positions;
    printLog(LOG_LOW, "subset sites (nseqs=%i, nsites=%i)\n",
             names.size(), positions.size());
    return 0;
}

template<>
int Sites::remove_overlapping(const TrackNullValue &track) {
    for (int i=positions.size()-1; i >= 0; i--) {
        if (track.index(positions[i]) != -1) {
            positions.erase(positions.begin() + i);
            cols.erase(cols.begin() + i);
        }
    }
    return 0;
}


template<>
void apply_mask_sequences<NullValue>(Sequences *sequences,
                                     const TrackNullValue &maskmap)
{
    const char maskchar = 'N';

    for (unsigned int k=0; k<maskmap.size(); k++) {
        for (int i=maskmap[k].start; i<maskmap[k].end; i++) {
            for (int j=0; j<sequences->get_num_seqs(); j++)
                sequences->seqs[j][i] = maskchar;
        }
    }
}



// Returns true if alignment column is invariant
static inline bool is_invariant_site(const char *const *seqs,
                                     const int nseqs, const int pos)
{
    const char c = seqs[0][pos];
    for (int j=1; j<nseqs; j++) {
        if (seqs[j][pos] != c) {
            return false;
        }
    }
    return true;
}


// Converts a Sequences alignment to a Sites alignment
void make_sites_from_sequences(const Sequences *sequences, Sites *sites)
{
    int nseqs = sequences->get_num_seqs();
    int seqlen = sequences->length();
    const char * const *seqs = sequences->get_seqs();

    sites->clear();
    sites->start_coord = 0;
    sites->end_coord = seqlen;
    sites->names.insert(sites->names.begin(),
                        sequences->names.begin(), sequences->names.end());

    for (int i=0; i<seqlen; i++) {
        if (!is_invariant_site(seqs, nseqs, i)) {
            char *col = new char [nseqs];
            for (int j=0; j<nseqs; j++)
                col[j] = seqs[j][i];
            sites->append(i, col);
        }
    }
}


bool Sequences::get_non_singleton_snp(vector<bool> &nonsing) {
    if (seqs.size() == 0) return false;
    for (int i=0; i < seqlen; i++) {
        char allele[4]={'N','N','N','N'};
        int count[4]={0,0,0,0};
        for (unsigned int j=0; j < seqs.size(); j++) {
            char c = seqs[j][i];
            if (c == 'N') continue;
            for (int k=0; k < 4; k++) {
                if (allele[k] == 'N')
                    allele[k] = c;
                if (allele[k] == c) {
                    count[k]++;
                    break;
                }
            }
        }
        int total = 0;
        for (int k=0; k < 4; k++)
            total += (count[k] > 1);
        nonsing[i] = (total >= 2);
    }
    return true;
}

void Sequences::set_pairs_by_name() {
  pairs.resize(names.size());
  for (unsigned int i=0; i < names.size(); i++) pairs[i] = -1;
  for (unsigned int i=0; i < names.size(); i++) {
    if (pairs[i] != -1) continue;
    int len = names[i].length();
    string basename = names[i].substr(0, len-2);
    string ext = names[i].substr(len-2, 2);
    string target_ext;
    if (ext == "_1") {
      target_ext = "_2";
    } else if (ext == "_2") {
      target_ext = "_1";
    } else {
      fprintf(stderr, "Error in set_pairs_by_name: names are not named with XXX_1/XXX_2 convention\n");
      exit(-1);
    }
    for (unsigned int j=i+1; j < names.size(); j++) {
      if (names[i].substr(0, len-2) == basename &&
	  names[j].substr(len-2, 2) == target_ext) {
	pairs[i] = j;
	pairs[j] = i;
	break;
      }
    }
    //	if (pairs[i] == -1); // do nothing, maybe this is ok sometimes
    // (ie, if we haven't added all sequences yet)
  }
}

void Sequences::set_pairs_from_file(string fn) {
  FILE *infile = fopen(fn.c_str(), "r");
  char p1[1000], p2[1000];
  if (infile == NULL) {
    fprintf(stderr, "Error opening %s\n", fn.c_str());
    exit(-1);
  }
  pairs = vector<int>(names.size());
  for (unsigned int i=0; i < names.size(); i++)
    pairs[i] = -1;
  int x;
  while (EOF != (x=fscanf(infile, "%s %s", p1, p2))) {
    if (x != 2) {
      fprintf(stderr, "Error: bad format for file %s\n", fn.c_str());
      exit(-1);
    }
    int x1=-1, x2=-1;
    for (unsigned int i=0; i < names.size(); i++) {
      if (names[i] == p1) {
	x1=i;
	if (x2 >= 0) break;
      } else if (names[i] == p2) {
	x2=i;
	if (x1 >= 0) break;
      }
    }
    if (x1 >=0 && x2 >= 0) {
      pairs[x1] = x2;
      pairs[x2] = x1;
    }
  }
  fclose(infile);
}


void Sequences::set_pops_from_file(string fn) {
    FILE *infile = fopen(fn.c_str(), "r");
    char seqname[1000];
    int pop;
    if (infile == NULL) {
        fprintf(stderr, "Error opening %s\n", fn.c_str());
        exit(-1);
    }
    pops = vector<int>(names.size());
    for (unsigned int i=0; i < names.size(); i++) pops[i]=-1;
    while (2 == fscanf(infile, "%s %i", seqname, &pop)) {
        unsigned int i=0;
        for (i=0; i < names.size(); i++) {
            if (names[i] == seqname) {
                pops[i] = pop;
                break;
            }
        }
        if (i == names.size()) {
            printError("set_pops_from_file: do not see sequence %s\n", seqname);
        }
    }
    for (unsigned int i=0; i < names.size(); i++) {
        if (pops[i] == -1) {
            printError("set_pops_from_file: sequence %s does not have assignment\n",
                       names[i].c_str());
            assert(0);
        }
    }
}



void Sequences::set_pairs(const ArgModel *mod) {
  if (mod->unphased_file != "")
    set_pairs_from_file(mod->unphased_file);
 // else set_pairs_by_name();
  else {
     pairs = vector<int>(names.size());
     for (unsigned int i=0; i < names.size(); i++) {
        if (i%2==0) pairs[i] = i+1;
        else pairs[i] = i-1;
     }
  }
}

void PhaseProbs::sample_phase(int *thread_path) {
    int sing_tot=0, sing_switch=0, non_sing_tot=0, non_sing_switch=0;
    if (probs.size() == 0)
        return;
    for (map<int,vector<double> >::iterator it=probs.begin(); it != probs.end();
       it++) {
        int coord = it->first;
        vector<double> prob = it->second;
        if (seqs->seqs[hap1][coord] != seqs->seqs[hap2][coord]) {
            if (frand() > prob[thread_path[coord]]) {
                seqs->switch_alleles(coord, hap1, hap2);
                if (non_singleton_snp[coord])
                    non_sing_switch++;
                else sing_switch++;
            }
            if (non_singleton_snp[coord])
                non_sing_tot++;
            else sing_tot++;
        }
    }
    printLog(LOG_LOW, "sample_phase %s %s size=%i %i frac_switch=%f %f\n",
             seqs->names[hap1].c_str(), seqs->names[hap2].c_str(),
             sing_tot, non_sing_tot,
             (double)sing_switch/sing_tot,
             (double)non_sing_switch/non_sing_tot);
}


void Sequences::randomize_phase(double frac) {
    printLog(LOG_LOW, "randomizing phase (frac=%f)\n", frac);
    int count=0, total=0;
    for (int i=0; i < seqlen; i++) {
        for (int j=0; j < (int)seqs.size(); j++) {
            if (pairs[j] < j) continue;
            total++;
            if (frand() < frac) {
                if (frand() < 0.5) {
                    switch_alleles(i, j, pairs[j]);
                    count++;
                }
            }
        }
    }
    printLog(LOG_LOW, "switched %i of %i (%f)\n", count, total,
             (double)count/(double)total);
}


void Sequences::set_age() {
    int nseqs = names.size();
    ages.resize(nseqs, 0);
    real_ages.resize(nseqs, 0);
}

void Sequences::set_age(string agefile, int ntimes, const double *times) {
    char currseq[1000];
    FILE *infile = fopen(agefile.c_str(), "r");
    double time;
    int nseqs=names.size();
    if (infile == NULL) {
	fprintf(stderr, "Error opening %s\n", agefile.c_str());
	exit(-1);
    }
    ages.resize(nseqs, 0);
    real_ages.resize(nseqs, 0);
    while (EOF != fscanf(infile, "%s %lf", currseq, &time)) {
	bool found=false;
	for (int i=0; i < nseqs; i++) {
	    if (strcmp(names[i].c_str(), currseq)==0) {
		real_ages[i] = time;

		// choose the discrete time interval closest to the real time
		double mindif = fabs(times[0] - time);
		int whichmin=0;
		for (int j=1; j < ntimes-1; j++) {
		    double tempdif = fabs(times[j]-time);
		    if (tempdif < mindif) {
			whichmin=j;
			mindif = tempdif;
		    }
		    if (times[j] > time) break;
		}
		ages[i] = whichmin;
                printLog(LOG_LOW, "rounded age for sample %s to %f\n",
                         currseq, times[whichmin]);
		found=true;
		break;
	    }
	}
	if (!found) {
	    fprintf(stderr,
		    "WARNING: could not find sequence %s (from %s) in sequences\n",
		    currseq, agefile.c_str());
	}
    }
    fclose(infile);
}

// Compress the sites by a factor of 'compress'.
//
// Return true if compression is successful.
//
// The compression maintains the following properties:
//
// - The coorindate system (start_coord, end_coord) will be adjusted to
//   roughly (0, seqlen / compress).
// - Every variant column is kept.
//
bool find_compress_cols(const Sites *sites, int compress,
                        SitesMapping *sites_mapping)
{
    const int ncols = sites->get_num_sites();

    int blocki = 0;
    int next_block = sites->start_coord + compress;
    int half_block = compress / 2;

    // record old coords
    sites_mapping->init(sites);

    // special case
    if (compress == 1) {
        for (int i=sites->start_coord; i<=sites->end_coord; i++) {
            sites_mapping->all_sites.push_back(i);
        }

        for (int i=0; i<ncols; i++) {
            int col = sites->positions[i];
            sites_mapping->old_sites.push_back(col);
            sites_mapping->new_sites.push_back(col - sites->start_coord);
        }

        // record new coords
        sites_mapping->new_start = 0;
        sites_mapping->new_end = sites->length();
        return true;
    }

    // iterate through variant sites
    for (int i=0; i<ncols; i++) {
        int col = sites->positions[i];

        // find next block with variant site
        while (col >= next_block) {
            sites_mapping->all_sites.push_back(next_block - half_block);
            next_block += compress;
            blocki++;
        }

        // record variant site.
        sites_mapping->old_sites.push_back(col);
        sites_mapping->new_sites.push_back(blocki);
        sites_mapping->all_sites.push_back(col);
        next_block += compress;
        blocki++;

        // each original site should be unique
        const int n = sites_mapping->all_sites.size();
        if (n > 1)
            assert(sites_mapping->all_sites[n-1] !=
                   sites_mapping->all_sites[n-2]);

        // Check whether compression is not possible
        if (next_block - compress > sites->end_coord && i != (ncols-1))
            return false;
    }

    // record non-variants at end of alignment
    while (sites->end_coord >= next_block) {
        sites_mapping->all_sites.push_back(next_block - half_block);
        next_block += compress;
        blocki++;
    }

    // record new coords
    sites_mapping->new_start = 0;
    int new_end = sites->length() / compress;
    if (ncols > 0)
        sites_mapping->new_end = max(sites_mapping->new_sites[ncols-1]+1,
                                     new_end);
    else
        sites_mapping->new_end = new_end;

    return true;
}


// Apply compression using sites_mapping.
void compress_sites(Sites *sites, const SitesMapping *sites_mapping)
{
    const int ncols = sites->cols.size();
    sites->start_coord = sites_mapping->new_start;
    sites->end_coord = sites_mapping->new_end;

    for (int i=0; i<ncols; i++)
        sites->positions[i] = sites_mapping->new_sites[i];
}


// Uncompress sites using sites_mapping.
void uncompress_sites(Sites *sites, const SitesMapping *sites_mapping)
{
    const int ncols = sites->cols.size();
    if (ncols > (int)sites_mapping->old_sites.size() ||
        sites_mapping->old_sites.size() != sites_mapping->new_sites.size()) {
        fprintf(stderr, "Error; uncompress_sites got incompatible sites_mapping\n");
        abort();
    }
    sites->start_coord = sites_mapping->old_start;
    sites->end_coord = sites_mapping->old_end;

    unsigned int j=0;
    for (int i=0; i<ncols; i++) {
        while (sites_mapping->new_sites[j] != sites->positions[i]) {
            j++;
            if (j == sites_mapping->new_sites.size()) {
                fprintf(stderr,
                        "Error; could not find position %i in sites mapping\n",
                         sites->positions[i]);
                abort();
            }
        }
        sites->positions[i] = sites_mapping->old_sites[j];
    }
}


TrackNullValue get_n_regions(const Sites &sites, int numN) {
    TrackNullValue track;
    int numhap = sites.get_num_seqs();
    for (int i=0; i < sites.get_num_sites(); i++) {
        const char *cols = sites.cols[i];
        int count=0;
        for (int j=0; j < numhap; j++) {
            if (cols[j] == 'N') {
                count++;
                if (count >= numN) break;
            }
        }
        if (count >= numN) {
            track.push_back(RegionNullValue(sites.chrom, sites.positions[i],
                                            sites.positions[i]+1, ' '));
        }
    }
    track.merge();
    return track;
}

TrackNullValue get_snp_clusters(const Sites &sites, int numsnp, int window) {
    TrackNullValue track;
    if (numsnp < 0 || numsnp > window) return track;
    if (numsnp == 0) {  // mask everything
        track.push_back(RegionNullValue(sites.chrom, sites.start_coord,
                                        sites.end_coord, ' '));
        return track;
    }
    int numsite = sites.get_num_sites();
    bool isSnp[numsite];

    for (int i=0; i < numsite; i++)
        isSnp[i] = sites.is_snp(i);

    for (int i=0; i < numsite; i++) {
        if (!isSnp[i]) continue;
        int count=1;
        int start_pos = sites.positions[i];
        int j=i+1;
        for ( ; j < numsite; j++) {
            if (sites.positions[j] - start_pos + 1 > window) break;
            if (isSnp[j]) count++;
        }
        j--;
        if (count >= numsnp) {
            int end_pos = sites.positions[j];
            assert(start_pos <= end_pos);
            end_pos++;  // sites positions are zero-based; want end non-inclusive
            int diff = window - (end_pos - start_pos);
            track.push_back(RegionNullValue(sites.chrom, start_pos - diff,
                                            end_pos + diff, ' '));
        }
    }
    track.merge();
    return track;
}


//=============================================================================
// assert functions


// Returns True if sequences pass all sanity checks.
bool check_sequences(Sequences *seqs)
{
    return check_sequences(
        seqs->get_num_seqs(), seqs->length(), seqs->get_seqs()) &&
        check_seq_names(seqs);
}


// Ensures that all characters in the alignment are sensible.
bool check_sequences(int nseqs, int seqlen, char **seqs)
{
    // check seqs
    for (int i=0; i<nseqs; i++) {
        for (int j=0; j<seqlen; j++) {
            char x = seqs[i][j];
            if (strchr("NnRrYyWwSsKkMmBbDdHhVv", x))
                // treat Ns as gaps
                x = '-';
            if (x != '-' && dna2int[(int) (unsigned char) x] == -1) {
                // an unknown character is in the alignment
                printError("unknown char '%c' (char code %d)\n", x, x);
                return false;
            }
        }
    }

    return true;
}


// Return True if all gene names are valid.
bool check_seq_names(Sequences *seqs)
{
    for (unsigned int i=0; i<seqs->names.size(); i++) {
        if (!check_seq_name(seqs->names[i].c_str())) {
            printError("sequence name has illegal characters '%s'",
                       seqs->names[i].c_str());
            return false;
        }
    }

    return true;
}


//
// A valid gene name and species name follows these rules:
//
// 1. the first and last characters of the ID are a-z A-Z 0-9 _ - .
// 2. the middle characters can be a-z A-Z 0-9 _ - . or the space character ' '.
// 3. the ID should not be purely numerical characters 0-9
// 4. the ID should be unique within a gene tree or within a species tree
//

bool check_seq_name(const char *name)
{
    int len = strlen(name);

    if (len == 0) {
        printError("name is zero length");
        return false;
    }

    // check rule 1
    if (name[0] == ' ' || name[len-1] == ' ') {
        printError("name starts or ends with a space '%c'");
        return false;
    }

    // check rule 2
    for (int i=0; i<len; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.' || c == ' ')) {
            printError("name contains illegal character '%c'", c);
            return false;
        }
    }

    // check rule 3
    int containsAlpha = false;
    for (int i=0; i<len; i++) {
        if (name[i] < '0' || name[i] > '9') {
            containsAlpha = true;
            break;
        }
    }
    if (!containsAlpha) {
        printError("name is purely numeric '%s'", name);
        return false;
    }

    return true;
}


//=============================================================================
// Misc

void resample_align(Sequences *aln, Sequences *aln2)
{
    assert(aln->get_num_seqs() == aln2->get_num_seqs());
    char **seqs = aln->get_seqs();
    char **seqs2 = aln2->get_seqs();

    for (int j=0; j<aln2->length(); j++) {
        // randomly choose a column (with replacement)
        int col = irand(aln->length());

        // copy column
        for (int i=0; i<aln2->get_num_seqs(); i++) {
            seqs2[i][j] = seqs[i][col];
        }
    }
}

PhaseProbs::PhaseProbs(int _hap1, int _treemap1, Sequences *_seqs,
			    const LocalTrees *trees, const ArgModel *model) {
  if (!model->unphased) return;
  hap1 = _hap1;
  treemap1 = _treemap1;
  seqs = _seqs;
  if ((int)seqs->pairs.size() != seqs->get_num_seqs()) {
    seqs->set_pairs(model);
  }
  hap2 = seqs->pairs[hap1];
  if (hap2 != -1) {
    updateTreeMap2(trees);
  } else treemap2 = -1;
  if (seqs->get_num_seqs() > 0) {
      non_singleton_snp = vector<bool>(seqs->length());
      seqs->get_non_singleton_snp(non_singleton_snp);
  }
}



void PhaseProbs::updateTreeMap2(const LocalTrees *tree) {
    for (unsigned int i=0; i < tree->seqids.size(); i++) {
	if (tree->seqids[i] == hap2) {
	    treemap2 = i;
	    return;
	}
    }
    treemap2 = -1;
    return;
}


//=============================================================================
// C interface
extern "C" {


Sites *arghmm_read_sites(const char *filename,
                         int subregion_start=-1, int subregion_end=-1)
{
    Sites *sites = new Sites();
    bool results = read_sites(filename, sites, subregion_start, subregion_end);
    if (!results) {
        delete sites;
        return NULL;
    }
    return sites;
}


void arghmm_delete_sites(Sites *sites)
{
    delete sites;
}


} // extern "C"
} // namespace argweaver
