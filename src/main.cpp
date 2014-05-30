/**
 * @file main.cpp
 * @brief Main program for Kolmogorov-Zabih disparity estimation with graph cuts
 * @author Pascal Monasse <monasse@imagine.enpc.fr>
 *
 * Copyright (c) 2012-2014, Pascal Monasse
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * You should have received a copy of the GNU General Pulic License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "match.h"
#include "cmdLine.h"
#include <ctime>
#include <cstdio>

/// Is the color image actually gray?
bool isGray(RGBImage im) {
    const int xsize=imGetXSize(im), ysize=imGetYSize(im);
    for(int y=0; y<ysize; y++)
        for(int x=0; x<xsize; x++)
            if(imRef(im,x,y).c[0] != imRef(im,x,y).c[1] ||
               imRef(im,x,y).c[0] != imRef(im,x,y).c[2])
                return false;
    return true;
}

/// Convert to gray level a color image (extract red channel)
void convert_gray(GeneralImage& im) {
    const int xsize=imGetXSize(im), ysize=imGetYSize(im);
    GrayImage g = (GrayImage)imNew(IMAGE_GRAY, xsize, ysize);
    for(int y=0; y<ysize; y++)
        for(int x=0; x<xsize; x++)
            imRef(g,x,y) = imRef((RGBImage)im,x,y).c[0];
    imFree(im);
    im = (GeneralImage)g;
}

/// Decode a string as a fraction. Accept also value AUTO (=-1).
bool GetFraction(const std::string& s, int& numerator, int& denominator) {
    if(s=="AUTO")     { numerator = -1;       denominator = 1; return true; }
    if(std::sscanf(s.c_str(), "%d/%d", &numerator, &denominator) != 2) {
        if(std::sscanf(s.c_str(), "%d", &numerator) == 1)
            denominator = 1;
    }
    bool ok = (numerator>=0 && denominator>=1);
    if(! ok)
        std::cerr << "Unable to decode " << s << " as fraction" << std::endl;
    return ok;
}

/// Multiply lambda, lambda, lambda1, lambda2, K, denominator by denom.
void multLambdaK(int& lambda, int denom[5], Match::Parameters& params) {
    lambda             *= denom[0];
    params.lambda1     *= denom[1];
    params.lambda2     *= denom[2];
    params.K           *= denom[3];
    params.denominator *= denom[4];
}

/// Set lambda to value lambda/denom. As we have to keep as int, we need to
/// modify the overall params.denominator in consequence.
void setLambda(int& lambda, int denom, Match::Parameters& params) {
    int mult[] = {params.denominator,denom,denom,denom,denom};
    multLambdaK(lambda, mult, params);
}

/// Set lambda1 to value lambda1/denom. See setLambda for explanation.
void setLambda1(int& lambda, int denom, Match::Parameters& params) {
    int mult[] = {denom,params.denominator,denom,denom,denom};
    multLambdaK(lambda, mult, params);
}

/// Set lambda2 to value lambda2/denom. See setLambda for explanation.
void setLambda2(int& lambda, int denom, Match::Parameters& params) {
    int mult[] = {denom,denom,params.denominator,denom,denom};
    multLambdaK(lambda, mult, params);
}

/// Set params.K to value params.K/denom. See setLambda for explanation.
void setK(int& lambda, int denom, Match::Parameters& params) {
    int mult[] = {denom,denom,denom,params.denominator,denom};
    multLambdaK(lambda, mult, params);
}

/// GCD of integers
int gcd(int a, int b) {
    if(b == 0) return a;
    int r = a % b;
    return gcd(b, r);
}

/// Make sure parameters K, lambda1 and lambda2 are non-negative.
///
/// - K may be computed automatically and lambda set to K/5.
/// - lambda1=3*lambda, lambda2=lambda
/// As the graph requires integer weights, use fractions and common denominator.
/// Return the denominator of lambda.
int fix_parameters(Match& m, Match::Parameters& params, int& lambda) {
    if(lambda<0) { // Set lambda to K/5
        float K = params.K/(float)params.denominator;
        if(params.K<=0) { // Automatic computation of K
            m.SetParameters(&params);
            K = m.GetK();
        }
        K /= 5; int denom = 1;
        while(K < 3) { K *= 2; denom *= 2; }
        lambda = int(K+0.5f);
        setLambda(lambda, denom, params);
    }
    if(params.K<0) params.K = 5*lambda;
    if(params.lambda1<0) params.lambda1 = 3*lambda;
    if(params.lambda2<0) params.lambda2 = lambda;
    int denom = gcd(params.K,
                    gcd(params.lambda1,
                            gcd(params.lambda2,
                                params.denominator)));
    int denomLambda = params.denominator;
    if(denom>1) { // Reduce fractions to minimize risk of overflow
        denomLambda = denom;
        params.K /= denom;
        params.lambda1 /= denom;
        params.lambda2 /= denom;
        params.denominator /= denom;
    }
    m.SetParameters(&params);
    // Reduce fraction lambda/denomLambda
    denom = gcd(lambda, denomLambda);
    if(denom>1) {
        lambda /= denom;
        denomLambda /= denom;
    }
    return denomLambda;
}

int main(int argc, char *argv[]) {
    // Default parameters
    Match::Parameters params = {
        Match::Parameters::L2, 1, // dataCost, denominator
        8, -1, -1, // edgeThresh, lambda1, lambda2 (smoothness cost)
        -1,        // K (occlusion cost)
        4, false   // maxIter, bRandomizeEveryIteration
    };

    CmdLine cmd;
    std::string cost, slambda, slambda1, slambda2, sK, sDisp;
    cmd.add( make_option('i', params.maxIter, "max_iter") );
    cmd.add( make_option('o', sDisp, "output") );
    cmd.add( make_switch('r', "random") );
    cmd.add( make_option('c', cost, "data_cost") );
    cmd.add( make_option('l', slambda, "lambda") );
    cmd.add( make_option(0, slambda1, "lambda1") );
    cmd.add( make_option(0, slambda2, "lambda2") );
    cmd.add( make_option('t', params.edgeThresh, "threshold") );
    cmd.add( make_option('k', sK) );

    cmd.process(argc, argv);
    if(argc != 5 && argc != 6) {
        std::cerr << "Usage: " << argv[0] << " [options] "
                  << "im1.png im2.png dMin dMax [dispMap.tif]" << std::endl;
        std::cerr << "General options:" << '\n'
                  << " -i,--max_iter iter: max number of iterations" <<'\n'
                  << " -o,--output disp.png: scaled disparity map" <<std::endl
                  << " -r,--random: random alpha order at each iteration" <<'\n'
                  << "Options for cost:" <<'\n'
                  << " -c,--data_cost dist: L1 or L2" <<'\n'
                  << " -l,--lambda lambda: value of lambda (smoothness)" <<'\n'
                  << " --lambda1 l1: smoothness cost not across edge" <<'\n'
                  << " --lambda2 l2: smoothness cost across edge" <<'\n'
                  << " -t,--threshold thres: intensity diff for 'edge'" <<'\n'
                  << " -k k: cost for occlusion" <<std::endl;
        return 1;
    }

    if( cmd.used('r') ) params.bRandomizeEveryIteration=true;
    if( cmd.used('c') ) {
        if(cost == "L1")
            params.dataCost = Match::Parameters::L1;
        else if(cost == "L2")
            params.dataCost = Match::Parameters::L2;
        else {
            std::cerr << "The cost parameter must be 'L1' or 'L2'" << std::endl;
            return 1;
        }
    }

    int lambda=-1;
    if(! slambda.empty()) {
        int denom;
        if(! GetFraction(slambda, lambda, denom)) return 1;
        setLambda(lambda, denom, params);
    }
    if(! slambda1.empty()) {
        int denom;
        if(! GetFraction(slambda1, params.lambda1, denom)) return 1;
        setLambda1(lambda, denom, params);
    }
    if(! slambda2.empty()) {
        int denom;
        if(! GetFraction(slambda2, params.lambda2, denom)) return 1;
        setLambda2(lambda, denom, params);
    }
    if(! sK.empty()) {
        int denom;
        if(! GetFraction(sK, params.K, denom)) return 1;
        setK(lambda, denom, params);
    }

    GeneralImage im1 = (GeneralImage)imLoad(IMAGE_RGB, argv[1]);
    GeneralImage im2 = (GeneralImage)imLoad(IMAGE_RGB, argv[2]);
    if(! im1) {
        std::cerr << "Unable to read image " << argv[1] << std::endl;
        return 1;
    }
    if(! im2) {
        std::cerr << "Unable to read image " << argv[2] << std::endl;
        return 1;
    }
    bool color=true;
    if(isGray((RGBImage)im1) && isGray((RGBImage)im2)) {
        color=false;
        convert_gray(im1);
        convert_gray(im2);
    }
    Match m(im1, im2, color);

    // Disparity
    int disp_base=0, disp_max=0;
    std::istringstream f(argv[3]), g(argv[4]);
    if(! ((f>>disp_base).eof() && (g>>disp_max).eof())) {
        std::cerr << "Error reading dMin or dMax" << std::endl;
        return 1;
    }
    m.SetDispRange(disp_base, disp_max);

    time_t seed = time(NULL);
    srand((unsigned int)seed);

    int denomLambda = fix_parameters(m, params, lambda);
    if(argc>5 || !sDisp.empty()) {
        m.KZ2();
        if(argc>5)
            m.SaveXLeft(argv[5]);
        if(! sDisp.empty())
            m.SaveScaledXLeft(sDisp.c_str(), false);
    } else {
        std::cout << "K=" << params.K;
        if(params.denominator!=1) std::cout << "/" << params.denominator;
        std::cout << std::endl;
        std::cout << "lambda=" << lambda;
        if(denomLambda!=1) std::cout << "/" << denomLambda;
        std::cout << std::endl;
    }

    imFree(im1);
    imFree(im2);
    return 0;
}
