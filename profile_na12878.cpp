/* The MIT License

   Copyright (c) 2014 Adrian Tan <atks@umich.edu>

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.
*/

#include "profile_na12878.h"

namespace
{
class OverlapStats
{
    public:

    uint32_t a,ab,b,a_ins,ab_ins,b_ins,a_del,ab_del,b_del;

    OverlapStats()
    {
        a = 0;
        ab = 0;
        b = 0;

        a_ins = 0;
        a_del = 0;
        ab_ins = 0;
        ab_del = 0;
        b_ins = 0;
        b_del = 0;
    };
};

class ConcordanceStats
{
    public:

    int32_t geno[4][4];

    ConcordanceStats()
    {
        for (int32_t i=0; i<4; ++i)
        {
            for (int32_t j=0; j<4; ++j)
            {
                geno[i][j] = 0;
            }
        }
    };
};

class Igor : Program
{
    public:

    std::string version;

    ///////////
    //options//
    ///////////
    std::string input_vcf_file;
    std::vector<std::string> input_vcf_files;
    std::string ref_fasta_file;
    std::string ref_data_sets_list;
    std::vector<GenomeInterval> intervals;
    std::string interval_list;
    std::vector<std::string> dataset_labels;
    std::vector<std::string> dataset_types;
    std::vector<OverlapStats> stats;
    std::vector<ConcordanceStats> concordance;
    std::string gencode_gtf_file;
    bool gencode_exists;
    std::string filter_expression;
    Filter filter;

    ///////
    //i/o//
    ///////
    BCFSyncedReader *sr;
    bcf1_t *v;
    kstring_t line;

    /////////
    //stats//
    /////////
    uint32_t no_variants;
    uint32_t nfs;
    uint32_t fs;
    uint32_t rare_nfs;
    uint32_t rare_fs;
    uint32_t common_nfs;
    uint32_t common_fs;

    ////////////////
    //common tools//
    ////////////////
    VariantManip *vm;
    GENCODE *gc;

    Igor(int argc, char ** argv)
    {
        //////////////////////////
        //options initialization//
        //////////////////////////
        try
        {
            std::string desc = "profile NA12878";

            version = "0.5";
            TCLAP::CmdLine cmd(desc, ' ', version);
            VTOutput my; cmd.setOutput(&my);
            TCLAP::ValueArg<std::string> arg_ref_fasta_file("r", "r", "reference sequence fasta file []", true, "", "str", cmd);
            TCLAP::ValueArg<std::string> arg_intervals("i", "i", "intervals []", false, "", "str", cmd);
            TCLAP::ValueArg<std::string> arg_interval_list("I", "I", "file containing list of intervals []", false, "", "file", cmd);
            TCLAP::ValueArg<std::string> arg_filter_expression("f", "f", "filter expression []", false, "", "str", cmd);
            TCLAP::ValueArg<std::string> arg_ref_data_sets_list("g", "g", "file containing list of reference datasets []", false, "", "file", cmd);
            TCLAP::UnlabeledValueArg<std::string> arg_input_vcf_file("<in.vcf>", "input VCF file", true, "","file", cmd);

            cmd.parse(argc, argv);

            ref_fasta_file = arg_ref_fasta_file.getValue();
            filter_expression = arg_filter_expression.getValue();
            parse_intervals(intervals, arg_interval_list.getValue(), arg_intervals.getValue());
            ref_data_sets_list = arg_ref_data_sets_list.getValue();
            input_vcf_file = arg_input_vcf_file.getValue();

            ///////////////////////
            //parse input VCF files
            ///////////////////////
        }
        catch (TCLAP::ArgException &e)
        {
            std::cerr << "error: " << e.error() << " for arg " << e.argId() << "\n";
            abort();
        }
    };

    void initialize()
    {
        //////////////////////
        //reference data set//
        //////////////////////
//# This file contains information on how to process reference data sets.
//#
//# dataset - name of data set, this label will be printed.
//# type    - True Positives (TP) and False Positives (FP)
//#           overlap percentages labeled as (Precision, Sensitivity) and (False Discovery Rate, Type I Error) respectively
//#         - annotation
//#           file is used for GENCODE annotation of frame shift and non frame shift Indels
//# filter  - filter applied to variants for this particular data set
//# path    - path of indexed BCF file
//#dataset              type         filter    path
//broad.kb              TP           PASS      /net/fantasia/home/atks/dev/vt/bundle/public/grch37/broad.kb.241365variants.genotypes.bcf
//illumina.platinum     TP           PASS      /net/fantasia/home/atks/dev/vt/bundle/public/grch37/NA12878.illumina.platinum.5284448variants.genotypes.bcf
//gencode.v19           annotation   .         /net/fantasia/home/atks/dev/vt/bundle/public/grch37/gencode.v19.annotation.gtf.gz

        input_vcf_files.push_back(input_vcf_file);
        dataset_labels.push_back("data");
        dataset_types.push_back("ref");

        filter.parse(filter_expression.c_str(), true);


        htsFile *hts = hts_open(ref_data_sets_list.c_str(), "r");
        kstring_t s = {0,0,0};
        std::vector<std::string> vec;
        while (hts_getline(hts, '\n', &s)>=0)
        {
            if (s.s[0] == '#')
                continue;

            std::string line(s.s);
            split(vec, " ", line);

            if (vec[1] == "TP" || vec[1] == "FP")
            {
                dataset_labels.push_back(vec[0]);
                dataset_types.push_back(vec[1]);
                input_vcf_files.push_back(vec[3]);
            }
            else if (vec[1] == "annotation")
            {
                gencode_gtf_file = vec[3];
            }
            else
            {
                std::cerr << "Reference data set type: \"" << vec[1] << "\" not recognised\n";
                exit(1);
            }
        }
        hts_close(hts);
        if (s.m) free(s.s);

        //////////////////////
        //i/o initialization//
        //////////////////////
        sr = new BCFSyncedReader(input_vcf_files, intervals, false);

        ///////////////////////
        //tool initialization//
        ///////////////////////
        vm = new VariantManip(ref_fasta_file);
        gencode_exists = false;
        if (gencode_gtf_file != "")
        {
            gencode_exists = true;
            gc = new GENCODE(gencode_gtf_file, ref_fasta_file);
        }

        ////////////////////////
        //stats initialization//
        ////////////////////////
        no_variants = 0;
        fs = 0;
        nfs = 0;
    }

    void profile_na12878()
    {
        //for combining the alleles
        std::vector<bcfptr*> current_recs;
        std::vector<Interval*> overlaps;
        Variant variant;
        int32_t no_overlap_files = input_vcf_files.size();
        std::vector<int32_t> presence(no_overlap_files, 0);
        std::vector<bcfptr*> presence_bcfptr(no_overlap_files, NULL);
        stats.resize(no_overlap_files);
        concordance.resize(no_overlap_files);

        int32_t *gts = NULL;
        int32_t n = 0;
        int32_t x1, x2, x;

        int32_t na12878_index[no_overlap_files];

        //get NA12878 gt position from reference file
        for (int32_t i=0; i<no_overlap_files; ++i)
        {
            na12878_index[i] = bcf_hdr_id2int(sr->hdrs[i], BCF_DT_SAMPLE, "NA12878");
        }

        int32_t discordance_filter = 0;

        while(sr->read_next_position(current_recs))
        {
            //check first variant
            bcf1_t *v = current_recs[0]->v;
            bcf_hdr_t *h = current_recs[0]->h;
            int32_t vtype = vm->classify_variant(h, v, variant);

            //if (filter.apply(h, v, &variant))
            //if (bcf_has_filter(h,v,"PASS")!=1)
            //if (bcf_get_n_allele(v)!=2 || !(vtype==VT_INDEL || vtype==(VT_SNP|VT_INDEL)) )
            //if (bcf_get_n_allele(v)!=2 )
            //if (bcf_get_n_allele(v)!=2 || !(vtype==VT_INDEL) || bcf_has_filter(h,v,"PASS")!=1 )
//            if (bcf_get_n_allele(v)!=2 || !(vtype==VT_INDEL || vtype==(VT_SNP|VT_INDEL)) )
//            if (bcf_get_n_allele(v)!=2 || !(vtype==VT_SNP) )
            //if (bcf_get_n_allele(v)!=2 || !(vtype==VT_INDEL) || variant.alleles[0].dlen!=1)
            
            //bool v1 = (bcf_get_n_allele(v)!=2 || variant.alleles[0].dlen==0);
            //bool v1 = !(bcf_get_n_allele(v)==2 && variant.alleles[0].dlen==1);
            bool v1 = false;
            //bool v2 = false;
            bool v2 = !filter.apply(h, v, &variant);
            if (false && v1!=v2)
            {
                ++discordance_filter;
                std::cerr << "discordance in filter:"  << "v1:" << v1 << " v2:" << v2 <<  "\n";
                bcf_print(h,v);
                variant.print();
            }
            
            if (v2)
            {
                continue;
            }
            std::string chrom = bcf_get_chrom(h,v);
            int32_t start1 = bcf_get_pos1(v);
            int32_t end1 = bcf_get_end_pos1(v);

            //check existence
            for (uint32_t i=0; i<current_recs.size(); ++i)
            {
                ++presence[current_recs[i]->file_index];
                presence_bcfptr[current_recs[i]->file_index] = current_recs[i];
            }

            //annotate
            if (presence[0])
            {
                if (gencode_exists)
                {
                    gc->search(chrom, start1+1, end1, overlaps);

                    bool cds_found = false;
                    bool is_fs = false;

                    for (int32_t i=0; i<overlaps.size(); ++i)
                    {
                        GENCODERecord *rec = (GENCODERecord *) overlaps[i];
                        if (rec->feature==GC_FT_CDS)
                        {
                            cds_found = true;
                            if (abs(variant.alleles[0].dlen)%3!=0)
                            {
                                is_fs = true;
                                break;
                            }
                        }
                    }

                    if (cds_found)
                    {
                        if (is_fs)
                        {
                            ++fs;
                        }
                        else
                        {
                            ++nfs;
                        }
                    }
                }

                ++no_variants;
            }

            int32_t ins = variant.alleles[0].ins;
            int32_t del = 1-ins;

            if (presence[0])
            {
                ++stats[0].a;
                stats[0].a_ins += ins;
                stats[0].a_del += del;

                bcf_unpack(v, BCF_UN_STR);
                int k = bcf_get_genotypes(presence_bcfptr[0]->h, presence_bcfptr[0]->v, &gts, &n);
                x1 = bcf_gt_allele(gts[na12878_index[0]*2]);
                x2 = bcf_gt_allele(gts[na12878_index[0]*2+1]);
                
                x = x1+x2;
                if (x==-2)
                {
                    x = 3;
                }
            }

            //update overlap stats
            for (uint32_t i=1; i<no_overlap_files; ++i)
            {
                if (presence[0] && !presence[i])
                {
                    ++stats[i].a;
                    stats[i].a_ins += ins;
                    stats[i].a_del += del;
                }
                else if (presence[0] && presence[i])
                {
                    ++stats[i].ab;
                    stats[i].ab_ins += ins;
                    stats[i].ab_del += del;

                    bcf_unpack(presence_bcfptr[i]->v, BCF_UN_IND);
                    int k = bcf_get_genotypes(presence_bcfptr[i]->h, presence_bcfptr[i]->v, &gts, &n);

                    int32_t y1 = bcf_gt_allele(gts[na12878_index[i]*2]);
                    int32_t y2 = bcf_gt_allele(gts[na12878_index[i]*2+1]);
                    int32_t y = y1+y2;
                    if (y==-2)
                    {
                        y = 3;
                    }

                    ++concordance[i].geno[x][y];
                }
                else if (!presence[0] && presence[i])
                {
                    ++stats[i].b;
                    stats[i].b_ins += ins;
                    stats[i].b_del += del;
                }
                else
                {
                    //not in either, do nothing
                }

                presence[i]=0;
                presence_bcfptr[i]=NULL;
            }

            presence[0] = 0;
        }
        
        std::cerr << "NO DISCORDANCE FILTERS " << discordance_filter << "\n";
        
    };

    void print_options()
    {
        std::clog << "profile_na12878 v" << version << "\n\n";
        std::clog << "\n";
        std::clog << "Options:     input VCF File                 " << input_vcf_file << "\n";
        std::clog << "         [g] reference data sets list file  " << ref_data_sets_list << "\n";
        std::clog << "         [r] reference FASTA file           " << ref_fasta_file << "\n";
        print_int_op("         [i] intervals                      ", intervals);
        std::clog << "\n";
   }

    void print_stats()
    {
        fprintf(stderr, "\n");
        fprintf(stderr, "  %s\n", "data set");
        fprintf(stderr, "    No Indels : %10d [%.2f]\n", stats[0].a,  (float)stats[0].a_ins/(stats[0].a_del));
        fprintf(stderr, "       FS/NFS : %10.2f (%d/%d)\n", (float)fs/(fs+nfs), fs, nfs);
        fprintf(stderr, "\n");

        for (int32_t i=1; i<dataset_labels.size(); ++i)
        {
            fprintf(stderr, "  %s\n", dataset_labels[i].c_str());
            fprintf(stderr, "    A-B %10d [%.2f]\n", stats[i].a,  (float)stats[i].a_ins/(stats[i].a_del));
            fprintf(stderr, "    A&B %10d [%.2f]\n", stats[i].ab, (float)stats[i].ab_ins/stats[i].ab_del);
            fprintf(stderr, "    B-A %10d [%.2f]\n", stats[i].b,  (float)stats[i].b_ins/(stats[i].b_del));

            if (dataset_types[i]=="TP")
            {
                fprintf(stderr, "    Precision    %4.1f%%\n", 100*(float)stats[i].ab/(stats[i].a+stats[i].ab));
                fprintf(stderr, "    Sensitivity  %4.1f%%\n", 100*(float)stats[i].ab/(stats[i].b+stats[i].ab));
            }
            else
            {
                fprintf(stderr, "    FDR          %4.1f%%\n", 100*(float)stats[i].ab/(stats[i].a+stats[i].ab));
                fprintf(stderr, "    Type I Error %4.1f%%\n", 100*(float)stats[i].ab/(stats[i].b+stats[i].ab));
            }
            fprintf(stderr, "\n");
        }

        for (int32_t i=1; i<dataset_labels.size(); ++i)
        {
            int32_t (&geno)[4][4] = concordance[i].geno;
            int32_t total = 0;
            int32_t concordance = 0;
            for (int32_t i=0; i<3; ++i)
            {
                for (int32_t j=0; j<3; ++j)
                {
                    total += geno[i][j];
                    if (i==j)
                    {
                        concordance += geno[i][j];
                    }
                }
            }

            int32_t discordance = total-concordance;

            fprintf(stderr, "  %s\n", dataset_labels[i].c_str());
            fprintf(stderr, "                R/R       R/A       A/A       ./.\n");
            fprintf(stderr, "    R/R    %8d  %8d  %8d  %8d\n", geno[0][0], geno[0][1], geno[0][2], geno[0][3]);
            fprintf(stderr, "    R/A    %8d  %8d  %8d  %8d\n", geno[1][0], geno[1][1], geno[1][2], geno[1][3]);
            fprintf(stderr, "    A/A    %8d  %8d  %8d  %8d\n", geno[2][0], geno[2][1], geno[2][2], geno[2][3]);
            fprintf(stderr, "    ./.    %8d  %8d  %8d  %8d\n", geno[3][0], geno[3][1], geno[3][2], geno[3][3]);
            fprintf(stderr, "\n");

            fprintf(stderr, "    Total genotype pairs :  %8d\n", total);
            fprintf(stderr, "    Concordance          :  %5.2f%% (%d)\n", (float)concordance/total*100, concordance);
            fprintf(stderr, "    Discordance          :  %5.2f%% (%d)\n", (float)discordance/total*100, discordance);

            fprintf(stderr, "\n");
        }

    };

    ~Igor()
    {
    };

    private:
};

}

void profile_na12878(int argc, char ** argv)
{
    Igor igor(argc, argv);
    igor.print_options();
    igor.initialize();
    igor.profile_na12878();
    igor.print_stats();
}