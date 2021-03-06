% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/misc.R
\name{probCoalCounts}
\alias{probCoalCounts}
\title{Probability of going from "a" lineages to "b" lineages}
\usage{
probCoalCounts(tOver2N, a, b)
}
\arguments{
\item{tOver2N}{time (in generations) divided by haploid popsize}

\item{a}{The starting number of lineages}

\item{b}{The ending number of lineages (should be <= a)}
}
\value{
Probability under coalescenct
}
\description{
Probability of going from "a" lineages to "b" lineages
}
